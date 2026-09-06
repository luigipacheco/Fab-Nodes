#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

// FabNodes protocol v1.1: availability ($state + LWT), latched emergency stop,
// retained-control replay guard, standard diagnostics, and mDNS broker resolution.
// MQTT client functions are templates so this header does not depend on a
// specific MQTT library (sketches use PubSubClient).

namespace FabNodes {

static constexpr const char* kStateOnline = "online";
static constexpr const char* kStateOffline = "offline";

inline String fabStateTopic(const String& nodeName) {
  return nodeName + "/$state";
}

inline String fabSafeStatusTopic(const String& nodeName) {
  return nodeName + "/status/safe";
}

// Parses control-style boolean payloads: "1"/"true"/"on"/"yes" or numeric >= 0.5.
inline bool fabParseBoolPayload(const byte* payload, unsigned int length) {
  String value = "";
  value.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) value += (char)payload[i];
  value.trim();
  if (value.length() == 0) return false;

  String lower = value;
  lower.toLowerCase();
  if (lower == "true" || lower == "1" || lower == "on" || lower == "yes") return true;
  if (lower == "false" || lower == "0" || lower == "off" || lower == "no") return false;
  return value.toFloat() >= 0.5f;
}

// ── MQTT connect with availability ──────────────────────────────────────────
// Connects with a Last Will of "offline" (retained, QoS 1) on <nodeName>/$state,
// then publishes retained "online" on success.
template <typename MqttClient>
inline bool fabMqttConnectWithState(
  MqttClient& client,
  const String& clientId,
  const String& username,
  const String& password,
  const String& nodeName) {
  String stateTopic = fabStateTopic(nodeName);

  bool ok;
  if (username.length() > 0) {
    ok = client.connect(clientId.c_str(), username.c_str(), password.c_str(),
                        stateTopic.c_str(), 1, true, kStateOffline);
  } else {
    ok = client.connect(clientId.c_str(),
                        stateTopic.c_str(), 1, true, kStateOffline);
  }

  if (ok) {
    client.publish(stateTopic.c_str(), kStateOnline, true);
  }
  return ok;
}

// ── Emergency stop (software stop) ───────────────────────────────────────────
// system/estop = 1 latches the node into its safe state until an explicit
// system/estop = 0. This is a software stop over the network — physical
// e-stops remain hardware's responsibility. Nodes already fail safe on
// connectivity loss, which is the underlying safety property.

struct FabEstopState {
  bool active = false;
  unsigned long changed_ms = 0;
};

inline bool fabIsEstopTopic(const char* topic) {
  return strcmp(topic, kEstopTopic) == 0;
}

// Returns true when the latch state changed.
inline bool fabEstopUpdate(FabEstopState& state, bool active, unsigned long now) {
  if (state.active == active) return false;
  state.active = active;
  state.changed_ms = now;
  return true;
}

// ── Retained-control replay guard ────────────────────────────────────────────
// Control topics must never be published retained, but a misbehaving tool can
// still do it. PubSubClient does not expose the retain flag, so control
// messages arriving inside a short window after subscribing are treated as
// retained replays and dropped. system/estop is exempt: it is retained on
// purpose and must always be applied.

struct FabControlReplayGuard {
  unsigned long subscribed_ms = 0;
  unsigned long window_ms = 1500;
  bool armed = false;
};

inline void fabMarkSubscribed(FabControlReplayGuard& guard, unsigned long now) {
  guard.subscribed_ms = now;
  guard.armed = true;
}

inline bool fabControlReplaySuspect(const FabControlReplayGuard& guard, unsigned long now) {
  return guard.armed && (now - guard.subscribed_ms) < guard.window_ms;
}

// ── Standard diagnostics ─────────────────────────────────────────────────────
// Publishes diag/rssi (dBm), diag/ip, diag/uptime (s) on a fixed interval.

struct FabDiagnostics {
  unsigned long interval_ms = 15000;
  unsigned long last_publish_ms = 0;
};

template <typename MqttClient>
inline void fabServiceDiagnostics(
  MqttClient& client,
  const String& nodeName,
  FabDiagnostics& diag,
  unsigned long now) {
  if (!client.connected()) return;
  if (diag.last_publish_ms != 0 && now - diag.last_publish_ms < diag.interval_ms) return;
  diag.last_publish_ms = now;

  String prefix = nodeName + "/diag/";
  client.publish((prefix + "rssi").c_str(), String(WiFi.RSSI()).c_str(), false);
  client.publish((prefix + "ip").c_str(), WiFi.localIP().toString().c_str(), false);
  client.publish((prefix + "uptime").c_str(), String(now / 1000UL).c_str(), false);
}

// ── mDNS broker resolution ───────────────────────────────────────────────────
// The hub advertises itself as fabnodes.local; ESP32 DNS cannot resolve .local
// hostnames, so they go through an explicit mDNS query. Requires fabStartMdns()
// after WiFi connects.

inline bool fabStartMdns(const String& nodeName) {
  return MDNS.begin(nodeName.c_str());
}

inline bool fabIsMdnsHost(const String& host) {
  return host.endsWith(".local");
}

inline IPAddress fabResolveMdnsHost(const String& host, uint32_t timeoutMs = 3000) {
  String name = host.substring(0, host.length() - 6);  // strip ".local"
  // Buffer copy: queryHost takes non-const char* on some ESP32 core versions
  char buf[64];
  name.toCharArray(buf, sizeof(buf));
  return MDNS.queryHost(buf, timeoutMs);
}

// Points the MQTT client at the broker, resolving .local hostnames via mDNS.
// `host` must outlive the client when passed as a plain hostname (setServer
// keeps the pointer).
template <typename MqttClient>
inline bool fabApplyBrokerEndpoint(MqttClient& client, const String& host, int port) {
  if (fabIsMdnsHost(host)) {
    IPAddress ip = fabResolveMdnsHost(host);
    if ((uint32_t)ip == 0) return false;
    client.setServer(ip, port);
    return true;
  }
  client.setServer(host.c_str(), port);
  return true;
}

}  // namespace FabNodes
