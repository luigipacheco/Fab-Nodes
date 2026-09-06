#pragma once

// FabNodesRuntime — the shared node lifecycle, so a sketch is only:
// hardware pins + FabSignal[] + a control callback + a safe-state callback.
//
// Owns: settings persistence, captive portal (shared HTML), WiFi connect and
// watchdog, MQTT reconnect with failure window, $state LWT, latched
// system/estop with status/safe ack, retained-control replay guard, manifest
// publishing, standard diagnostics, mDNS, and the default serial commands.
//
// Not included from FabNodesCore.h on purpose: it pulls in AsyncWebServer,
// DNSServer, and PubSubClient. New-style sketches include <FabNodesRuntime.h>.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <functional>
#include <vector>
#include "FabNodesCore.h"

namespace FabNodes {

// Shared captive-portal page. {{fields}} are replaced manually (the
// AsyncWebServer %-template engine would eat the CSS percent signs).
static const char kPortalHtml[] PROGMEM = R"html(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{node_type}} setup</title><style>
body{background:#16181d;color:#e6e9ef;font-family:system-ui,sans-serif;max-width:420px;margin:0 auto;padding:24px}
h1{font-size:20px}h1 span{color:#ff6a2b}p.sub{color:#8b93a3;font-size:13px;margin:4px 0 18px}
label{display:block;font-size:12px;color:#8b93a3;margin:12px 0 4px}
input{width:100%;box-sizing:border-box;padding:9px;background:#1f232b;border:1px solid #333a47;border-radius:6px;color:#e6e9ef;font-size:15px}
button{margin-top:20px;width:100%;padding:12px;background:#ff6a2b;color:#fff;font-weight:600;border:none;border-radius:8px;font-size:15px}
</style></head><body>
<h1><span>Fab</span>Nodes — {{node_type}}</h1>
<p class="sub">fw {{fw}} · {{protocol}}</p>
<form action="/save" method="POST">
<label>WiFi network</label><input name="wifi_ssid" value="{{wifi_ssid}}">
<label>WiFi password</label><input name="wifi_password" type="password">
<label>MQTT broker</label><input name="mqtt_server" value="{{mqtt_server}}">
<label>MQTT port</label><input name="mqtt_port" value="{{mqtt_port}}">
<label>MQTT username</label><input name="mqtt_username" value="{{mqtt_username}}">
<label>MQTT password</label><input name="mqtt_password" type="password">
<label>Node name (unique on your network)</label><input name="node_name" value="{{node_name}}">
<button type="submit">Save &amp; restart</button>
</form></body></html>)html";

struct FabRuntimeConfig {
  const char* node_type = "fabnode";
  const char* fw_version = "0.0.0";
  // Node name from the sketch's config.h (NODE_NAME). Seeds the protocol
  // nodeName and the setup AP ("fabnodes-<node_name>") on a device that has
  // never been through the portal. A name saved in the portal wins over this.
  const char* node_name = nullptr;
  const FabSignal* signals = nullptr;
  size_t signal_count = 0;
  uint16_t mqtt_buffer_size = 2048;

  // Called for every control message (estop and replay-guarded traffic
  // already filtered out). suffix is the topic below <nodeName>/.
  void (*on_control)(const String& suffix, const byte* payload, unsigned int length) = nullptr;
  // Force outputs into their safe state (estop latch, connectivity loss).
  void (*on_safe_state)() = nullptr;
  // Periodic application hook (sensors, buttons, displays). Runs every loop,
  // including in setup mode — check runtime.setupMode() when that matters.
  void (*on_sample)(unsigned long now) = nullptr;
  // After every successful MQTT connect (subscriptions + manifest done).
  void (*on_connected)() = nullptr;

  // Serial extensions (all optional): wired into the default command set.
  void (*print_values)() = nullptr;         // "values" command
  void (*print_custom_status)() = nullptr;  // appended to "status"
  void (*print_custom_help)() = nullptr;    // appended to "help"
  bool (*on_serial)(const String& command) = nullptr;  // custom commands; return true if handled
};

class FabNodeRuntime {
 public:
  bool begin(const FabRuntimeConfig& cfg) {
    cfg_ = cfg;
    instanceRef() = this;

    Serial.printf("\n\n=== %s — FabNodes %s ===\n", cfg_.node_type, kProtocolV11);
    client_id_ = String(cfg_.node_type) + "_" + fabChipId();
    Serial.printf("Client ID: %s\n", client_id_.c_str());

    loadNetworkSettings(prefs_, settings_);
    if (settings_.node_name.length() == 0 && cfg_.node_name != nullptr) {
      settings_.node_name = sanitizeNodeName(cfg_.node_name);
    }
    if (settings_.node_name.length() == 0) {
      settings_.node_name = defaultNodeName(cfg_.node_type);
    }
    Serial.printf("FabNode nodeName: %s\n", settings_.node_name.c_str());

    mqtt_.setClient(net_);
    mqtt_.setCallback(mqttTrampoline);
    mqtt_.setBufferSize(cfg_.mqtt_buffer_size);
    sig_rt_.assign(cfg_.signal_count, FabSignalRuntime());

    setupWebServer();
    connectToWiFi();
    return !setup_mode_;
  }

  void loop() {
    handleSerial();

    if (setup_mode_) {
      dns_.processNextRequest();
      if (cfg_.on_sample) cfg_.on_sample(millis());
      delay(50);
      return;
    }

    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
      if (was_connected_) { was_connected_ = false; enterSafe("WiFi lost"); }
      if (now - last_wifi_attempt_ >= kWifiReconnectMs) {
        last_wifi_attempt_ = now;
        Serial.println("[WiFi] Disconnected - reconnecting...");
        WiFi.reconnect();
      }
    } else if (mqtt_.connected()) {
      was_connected_ = true;
      mqtt_.loop();
      fabServiceDiagnostics(mqtt_, settings_.node_name, diag_, now);
    } else {
      if (was_connected_) { was_connected_ = false; enterSafe("MQTT lost"); }
      if (now - last_mqtt_attempt_ >= kMqttReconnectMs) {
        last_mqtt_attempt_ = now;
        reconnectMQTT();
      }
    }

    if (cfg_.on_sample) cfg_.on_sample(now);
  }

  // ── API for sketches ───────────────────────────────────────────────────
  bool publish(const String& suffix, const String& value, bool retained = false) {
    if (!mqtt_.connected()) return false;
    return mqtt_.publish((settings_.node_name + "/" + suffix).c_str(), value.c_str(), retained);
  }

  // ── Typed control bindings ───────────────────────────────────────────────
  // Register per-suffix handlers instead of hand-rolling dispatch in
  // on_control: the runtime parses the payload and clamps/validates it
  // against the signal's declared metadata before calling the handler.
  // Bindings win over on_control; unmatched suffixes still fall through to
  // it. Register any time (usually right after begin()).
  void onBool(const char* suffix, std::function<void(bool)> fn) {
    addBinding(suffix, [fn](const FabSignal*, const byte* p, unsigned int n) {
      fn(fabParseBoolPayload(p, n));
    });
  }
  void onInt(const char* suffix, std::function<void(long)> fn) {
    addBinding(suffix, [fn](const FabSignal* s, const byte* p, unsigned int n) {
      float v = payloadToString(p, n).toFloat();
      fn((long)lroundf(clampToSignal(s, v)));
    });
  }
  void onFloat(const char* suffix, std::function<void(float)> fn) {
    addBinding(suffix, [fn](const FabSignal* s, const byte* p, unsigned int n) {
      fn(clampToSignal(s, payloadToString(p, n).toFloat()));
    });
  }
  // For signals with enum options, values outside the declared set are
  // dropped (with a serial note) — the handler only ever sees valid options.
  void onString(const char* suffix, std::function<void(const String&)> fn) {
    addBinding(suffix, [fn](const FabSignal* s, const byte* p, unsigned int n) {
      String v = payloadToString(p, n);
      if (s && !fabOptionAllowed(*s, v)) {
        Serial.printf("[BIND] %s: '%s' is not one of [%s] - ignored\n",
                      s->suffix, v.c_str(), s->options);
        return;
      }
      fn(v);
    });
  }

  // ── Policy-driven publishing ─────────────────────────────────────────────
  // Publish a pub-signal value honoring its declared publish_policy,
  // deadband, and min/max intervals; formats by declared type (bool → 0/1,
  // int → integer, float → `decimals` places). Returns true when a publish
  // actually went out. Unknown suffixes publish plainly.
  bool set(const char* suffix, float value, int decimals = 3) {
    int idx = findSignal(suffix);
    if (idx < 0) return publish(suffix, String(value, decimals));
    const FabSignal& s = cfg_.signals[idx];
    FabSignalRuntime& rt = sig_rt_[idx];
    unsigned long now = millis();
    rt.current.f = value;
    rt.last_sample_ms = now ? now : 1;
    if (!fabPublishDue(s, rt, value, now)) return false;
    String payload = s.type == FAB_BOOL ? String(value >= 0.5f ? "1" : "0")
                   : s.type == FAB_INT  ? String((long)lroundf(value))
                                        : String(value, decimals);
    if (!publish(suffix, payload, s.retained)) return false;
    rt.last_published.f = value;
    rt.last_publish_ms = now;
    rt.publish_count++;
    return true;
  }
  bool set(const char* suffix, const String& value) {
    int idx = findSignal(suffix);
    if (idx < 0) return publish(suffix, value);
    const FabSignal& s = cfg_.signals[idx];
    FabSignalRuntime& rt = sig_rt_[idx];
    unsigned long now = millis();
    rt.last_sample_ms = now ? now : 1;
    if (!fabPublishDueStr(s, rt, value, now)) return false;
    if (!publish(suffix, value, s.retained)) return false;
    rt.str_value = value;
    rt.last_publish_ms = now;
    rt.publish_count++;
    return true;
  }
  // Last numeric value handed to set() for this signal; NAN before the first.
  float signalValue(const char* suffix) const {
    int idx = findSignal(suffix);
    if (idx < 0 || sig_rt_[idx].last_sample_ms == 0) return NAN;
    return sig_rt_[idx].current.f;
  }
  bool publishRaw(const char* topic, const String& value, bool retained = false) {
    if (!mqtt_.connected()) return false;
    return mqtt_.publish(topic, value.c_str(), retained);
  }
  bool connected() { return mqtt_.connected(); }
  bool estopActive() const { return estop_.active; }
  bool setupMode() const { return setup_mode_; }
  const String& nodeName() const { return settings_.node_name; }
  PubSubClient& mqtt() { return mqtt_; }

 private:
  static constexpr unsigned long kWifiReconnectMs = 10000;
  static constexpr unsigned long kMqttReconnectMs = 5000;
  static constexpr unsigned long kMqttFailWindowMs = 60000;
  static constexpr int kMqttFailThreshold = 12;
  static constexpr int kWifiAttempts = 30;

  // C++11-safe header-only singleton storage
  static FabNodeRuntime*& instanceRef() {
    static FabNodeRuntime* p = nullptr;
    return p;
  }

  struct ControlBinding {
    String suffix;
    std::function<void(const FabSignal*, const byte*, unsigned int)> fn;
  };
  std::vector<ControlBinding> bindings_;
  std::vector<FabSignalRuntime> sig_rt_;

  void addBinding(const char* suffix,
                  std::function<void(const FabSignal*, const byte*, unsigned int)> fn) {
    ControlBinding b;
    b.suffix = suffix;
    b.fn = fn;
    bindings_.push_back(b);
  }

  int findSignal(const char* suffix) const {
    for (size_t i = 0; i < cfg_.signal_count; i++) {
      if (strcmp(cfg_.signals[i].suffix, suffix) == 0) return (int)i;
    }
    return -1;
  }

  static String payloadToString(const byte* payload, unsigned int length) {
    String s;
    s.reserve(length);
    for (unsigned int i = 0; i < length; i++) s += (char)payload[i];
    return s;
  }

  static float clampToSignal(const FabSignal* s, float v) {
    if (!s) return v;
    if (s->has_min && v < s->min_value) v = s->min_value;
    if (s->has_max && v > s->max_value) v = s->max_value;
    return v;
  }
  FabRuntimeConfig cfg_;
  Preferences prefs_;
  NetworkSettings settings_;
  String client_id_;
  WiFiClient net_;
  PubSubClient mqtt_;
  AsyncWebServer server_{80};
  DNSServer dns_;
  FabEstopState estop_;
  FabControlReplayGuard guard_;
  FabDiagnostics diag_;
  bool setup_mode_ = false;
  bool was_connected_ = false;
  unsigned long last_wifi_attempt_ = 0;
  unsigned long last_mqtt_attempt_ = 0;
  unsigned long mqtt_fail_window_ = 0;
  int mqtt_failures_ = 0;

  static void mqttTrampoline(char* topic, byte* payload, unsigned int length) {
    if (instanceRef()) instanceRef()->onMessage(topic, payload, length);
  }

  void enterSafe(const char* reason) {
    Serial.printf("[SAFE] Entering safe state (%s)\n", reason);
    if (cfg_.on_safe_state) cfg_.on_safe_state();
  }

  void publishSafeStatus() {
    if (!mqtt_.connected()) return;
    mqtt_.publish(fabSafeStatusTopic(settings_.node_name).c_str(),
                  estop_.active ? "1" : "0", true);
  }

  void onMessage(char* topic, byte* payload, unsigned int length) {
    if (fabIsEstopTopic(topic)) {
      bool active = fabParseBoolPayload(payload, length);
      if (fabEstopUpdate(estop_, active, millis())) {
        if (estop_.active) {
          enterSafe("system/estop");
          Serial.println("[ESTOP] Latched - controls ignored until system/estop = 0");
        } else {
          Serial.println("[ESTOP] Cleared - controls accepted again (outputs stay safe until commanded)");
        }
        publishSafeStatus();
      }
      return;
    }
    if (estop_.active) {
      Serial.printf("[ESTOP] Ignoring control on %s while latched\n", topic);
      return;
    }
    if (fabControlReplaySuspect(guard_, millis())) {
      Serial.printf("[MQTT] Ignoring likely retained replay on %s\n", topic);
      return;
    }

    String t(topic);
    String prefix = settings_.node_name + "/";
    if (!t.startsWith(prefix)) return;
    String suffix = t.substring(prefix.length());
    for (const ControlBinding& b : bindings_) {
      if (b.suffix == suffix) {
        int idx = findSignal(suffix.c_str());
        b.fn(idx >= 0 ? &cfg_.signals[idx] : nullptr, payload, length);
        return;
      }
    }
    if (cfg_.on_control) cfg_.on_control(suffix, payload, length);
  }

  void reconnectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    Serial.printf("[MQTT] Connecting to %s:%d  clientId=%s\n",
      settings_.mqtt_server.c_str(), settings_.mqtt_port, client_id_.c_str());

    bool ok = false;
    if (!fabApplyBrokerEndpoint(mqtt_, settings_.mqtt_server, settings_.mqtt_port)) {
      Serial.printf("[MQTT] mDNS resolve failed for %s\n", settings_.mqtt_server.c_str());
    } else {
      ok = fabMqttConnectWithState(mqtt_, client_id_,
        settings_.mqtt_username, settings_.mqtt_password, settings_.node_name);
    }

    if (ok) {
      mqtt_failures_ = 0;
      mqtt_fail_window_ = 0;
      Serial.println("[MQTT] Connected");
      subscribeAll();
      publishManifest();
      publishSafeStatus();
      if (cfg_.on_connected) cfg_.on_connected();
    } else {
      unsigned long now = millis();
      if (mqtt_fail_window_ == 0 || now - mqtt_fail_window_ > kMqttFailWindowMs) {
        mqtt_fail_window_ = now;
        mqtt_failures_ = 0;
      }
      mqtt_failures_++;
      Serial.printf("[MQTT] FAILED state=%d fail=%d/%d\n",
        mqtt_.state(), mqtt_failures_, kMqttFailThreshold);
      if (mqtt_failures_ >= kMqttFailThreshold &&
          now - mqtt_fail_window_ <= kMqttFailWindowMs) {
        Serial.println("[MQTT] Repeated failures - entering setup mode");
        enterSafe("MQTT failure threshold");
        startSetupMode();
      }
    }
  }

  void subscribeAll() {
    for (size_t i = 0; i < cfg_.signal_count; i++) {
      const FabSignal& s = cfg_.signals[i];
      if (s.dir == FAB_SIG_SUB || s.dir == FAB_SIG_PUBSUB) {
        mqtt_.subscribe(fabSignalFullTopic(settings_.node_name, s).c_str());
      }
    }
    mqtt_.subscribe(kEstopTopic, 1);
    fabMarkSubscribed(guard_, millis());
  }

  void publishManifest() {
    FabManifestInfo info;
    info.node_name = settings_.node_name;
    info.node_type = cfg_.node_type;
    info.firmware_version = cfg_.fw_version;
    info.protocol_version = kProtocolV11;
    info.ip_address = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
    info.chip_id = fabChipId();

    String payload = fabBuildManifestPayload(info, cfg_.signals, cfg_.signal_count);
    String infoTopic = fabManifestInfoTopic(settings_.node_name);
    String mirror = fabManifestMirrorTopic(settings_.node_name, kManifestRoot);
    bool okInfo = mqtt_.publish(infoTopic.c_str(), payload.c_str(), true);
    bool okMirror = mqtt_.publish(mirror.c_str(), payload.c_str(), true);
    Serial.printf("[MANIFEST] %s (%s)  mirror=%s (%s)\n",
      infoTopic.c_str(), okInfo ? "OK" : "FAIL",
      mirror.c_str(), okMirror ? "OK" : "FAIL");
  }

  // ── Setup portal ─────────────────────────────────────────────────────────
  void setupWebServer() {
    server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
      String html = FPSTR(kPortalHtml);
      html.replace("{{node_type}}", cfg_.node_type);
      html.replace("{{fw}}", cfg_.fw_version);
      html.replace("{{protocol}}", kProtocolV11);
      html.replace("{{wifi_ssid}}", settings_.wifi_ssid);
      html.replace("{{mqtt_server}}",
        settings_.mqtt_server.length() ? settings_.mqtt_server : String(kDefaultBrokerHost));
      html.replace("{{mqtt_port}}", String(settings_.mqtt_port));
      html.replace("{{mqtt_username}}", settings_.mqtt_username);
      html.replace("{{node_name}}", settings_.node_name);
      req->send(200, "text/html", html);
    });
    server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
      auto param = [&](const char* name) -> String {
        return req->hasParam(name, true) ? req->getParam(name, true)->value() : String("");
      };
      if (req->hasParam("wifi_ssid", true)) settings_.wifi_ssid = param("wifi_ssid");
      String wp = param("wifi_password");
      if (wp.length() > 0) settings_.wifi_password = wp;
      if (req->hasParam("mqtt_server", true)) settings_.mqtt_server = param("mqtt_server");
      int port = param("mqtt_port").toInt();
      if (port > 0) settings_.mqtt_port = port;
      if (req->hasParam("mqtt_username", true)) settings_.mqtt_username = param("mqtt_username");
      String mp = param("mqtt_password");
      if (mp.length() > 0) settings_.mqtt_password = mp;
      if (req->hasParam("node_name", true)) settings_.node_name = sanitizeNodeName(param("node_name"));

      saveNetworkSettings(prefs_, settings_);
      req->send(200, "text/html",
        "<h1>Saved</h1><p>Device restarting…</p>"
        "<script>setTimeout(()=>location.href='/',3000)</script>");
      delay(1000);
      ESP.restart();
    });
    server_.onNotFound([](AsyncWebServerRequest* req) { req->redirect("/"); });
  }

  String processorVar(const String& var) {
    if (var == "node_type") return String(cfg_.node_type);
    if (var == "fw") return String(cfg_.fw_version);
    if (var == "protocol") return String(kProtocolV11);
    if (var == "wifi_ssid") return settings_.wifi_ssid;
    if (var == "mqtt_server")
      return settings_.mqtt_server.length() ? settings_.mqtt_server : String(kDefaultBrokerHost);
    if (var == "mqtt_port") return String(settings_.mqtt_port);
    if (var == "mqtt_username") return settings_.mqtt_username;
    if (var == "node_name") return settings_.node_name;
    return String();
  }

  void startSetupMode() {
    setup_mode_ = true;
    String ap = fabSetupApSsid(settings_.node_name);
    Serial.printf("=== Setup Mode ===\nAP: %s  |  http://192.168.4.1\n", ap.c_str());
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap.c_str());
    dns_.start(53, "*", IPAddress(192, 168, 4, 1));
    server_.begin();
  }

  void connectToWiFi() {
    if (settings_.wifi_ssid.length() == 0 || settings_.mqtt_server.length() == 0) {
      Serial.println("No WiFi/MQTT configuration - entering setup mode");
      startSetupMode();
      return;
    }
    Serial.printf("Connecting to WiFi: %s\n", settings_.wifi_ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings_.wifi_ssid.c_str(), settings_.wifi_password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < kWifiAttempts) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected - IP: %s RSSI: %d dBm\n",
        WiFi.localIP().toString().c_str(), WiFi.RSSI());
      if (fabStartMdns(settings_.node_name)) {
        Serial.printf("mDNS started - %s.local\n", settings_.node_name.c_str());
      }
      server_.begin();
    } else {
      Serial.println("WiFi failed - entering setup mode");
      startSetupMode();
    }
  }

  // ── Serial commands ──────────────────────────────────────────────────────
  void handleSerial() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');

    FabSerialStatusSnapshot snap;
    snap.title = cfg_.node_type;
    snap.node_name = settings_.node_name;
    snap.node_type = cfg_.node_type;
    snap.protocol_version = kProtocolV11;
    snap.wifi_connected = WiFi.status() == WL_CONNECTED;
    snap.wifi_ssid = settings_.wifi_ssid;
    snap.wifi_ip = snap.wifi_connected ? WiFi.localIP().toString() : String("");
    snap.mqtt_connected = mqtt_.connected();
    snap.mqtt_server = settings_.mqtt_server;
    snap.mqtt_port = settings_.mqtt_port;
    snap.signal_count = cfg_.signal_count;
    snap.setup_mode = setup_mode_;

    FabSerialHandlers handlers;
    handlers.print_values = cfg_.print_values;
    handlers.print_custom_status = cfg_.print_custom_status;
    handlers.print_custom_help = cfg_.print_custom_help;
    handlers.handle_custom_command = cfg_.on_serial;
    handlers.print_topics = [] {
      FabNodeRuntime* self = instanceRef();
      if (!self) return;
      for (size_t i = 0; i < self->cfg_.signal_count; i++) {
        const FabSignal& s = self->cfg_.signals[i];
        Serial.printf("%-4s %s\n", fabSignalDirName(s.dir),
          fabSignalFullTopic(self->settings_.node_name, s).c_str());
      }
      Serial.printf("sub  %s (QoS 1, latched)\n", kEstopTopic);
    };

    FabSerialCommandResult result =
      fabHandleSerialCommand(cmd, settings_.mqtt_port, snap, handlers);

    if (result.reset_requested) {
      Serial.println("Clearing configuration - restarting into setup mode");
      clearNetworkSettings(prefs_);
      delay(500);
      ESP.restart();
    } else if (result.broker_update_requested) {
      settings_.mqtt_server = result.broker_endpoint.host;
      settings_.mqtt_port = result.broker_endpoint.port;
      saveBrokerSettings(prefs_, settings_.mqtt_server, settings_.mqtt_port);
      Serial.printf("MQTT broker updated to %s:%d\n",
        settings_.mqtt_server.c_str(), settings_.mqtt_port);
      mqtt_.disconnect();
      last_mqtt_attempt_ = 0;
    } else if (result.broker_command_received) {
      Serial.println("Invalid broker command. Use broker=IP or broker=IP:PORT");
    } else if (!result.handled) {
      cmd.trim();
      if (cmd.length()) Serial.printf("Unknown command: %s\n", cmd.c_str());
    }
  }
};

}  // namespace FabNodes
