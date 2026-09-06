#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace FabNodes {

static constexpr const char* kSetupApSsid = "fabnodes";
static constexpr const char* kManifestRoot = "fabnodes/manifest";
static constexpr const char* kProtocolV1 = "fabnodes/1.0";
static constexpr const char* kProtocolV11 = "fabnodes/1.1";
static constexpr const char* kEstopTopic = "system/estop";
static constexpr const char* kDefaultBrokerHost = "fabnodes.local";
static constexpr int kDefaultMqttPort = 1883;

struct NetworkSettings {
  String wifi_ssid = "";
  String wifi_password = "";
  String mqtt_server = "";
  int mqtt_port = kDefaultMqttPort;
  String mqtt_username = "";
  String mqtt_password = "";
  String node_name = "";
};

struct BrokerEndpoint {
  String host = "";
  int port = kDefaultMqttPort;
};

inline String sanitizeNodeName(const String& input) {
  String out = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (ok) out += c;
    else if (c == ' ' || c == '.' || c == '/') out += '_';
  }
  while (out.length() > 0 && (out[0] == '_' || out[0] == '-')) out.remove(0, 1);
  while (out.length() > 0 && (out[out.length() - 1] == '_' || out[out.length() - 1] == '-')) {
    out.remove(out.length() - 1);
  }
  if (out.length() > 32) out = out.substring(0, 32);
  return out;
}

inline String jsonEscape(const String& input) {
  String out = "";
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

// Per-chip id. ESP.getEfuseMac() fills the six MAC bytes into a uint64 in
// address order, so on little-endian the LOW half is mac[0..3] — three of
// those four bytes are the Espressif OUI, identical on every board of a
// batch. The device-unique bytes are mac[3..5], up in the high half; a whole
// shelf of ESP32-S3s only differs there. Zero-padded so the width is stable.
inline String fabChipId() {
  uint32_t unique = (uint32_t)((ESP.getEfuseMac() >> 24) & 0xFFFFFFULL);
  char buf[7];
  snprintf(buf, sizeof(buf), "%06x", unique);
  return String(buf);
}

inline String defaultNodeName(const String& prefix) {
  return sanitizeNodeName(prefix + "_" + fabChipId());
}

// Setup AP name: "fabnodes-" + config.h NODE_NAME (fabnodes-fabnodes_fab-led),
// so a room full of fresh nodes (workshops!) broadcasts networks you can tell
// apart and match to a device. Falls back to the chip id when no node name is
// configured yet. Capped at the 32-byte SSID limit.
inline String fabSetupApSsid(const String& node_name = String()) {
  String suffix = sanitizeNodeName(node_name);
  if (suffix.length() == 0) suffix = fabChipId();
  String ssid = String(kSetupApSsid) + "-" + suffix;
  if (ssid.length() > 32) ssid = ssid.substring(0, 32);
  return ssid;
}

inline String activeNodeName(const String& node_name, const String& fallbackClientId) {
  if (node_name.length() > 0) return node_name;
  String fallback = sanitizeNodeName(fallbackClientId);
  return fallback.length() > 0 ? fallback : String("fabnode");
}

inline void loadNetworkSettings(Preferences& preferences, NetworkSettings& settings, const char* legacyNodeKey = nullptr) {
  preferences.begin("settings", true);
  settings.wifi_ssid = preferences.getString("wifi_ssid", "");
  settings.wifi_password = preferences.getString("wifi_password", "");
  settings.mqtt_server = preferences.getString("mqtt_server", "");
  settings.mqtt_port = preferences.getInt("mqtt_port", kDefaultMqttPort);
  settings.mqtt_username = preferences.getString("mqtt_username", "");
  settings.mqtt_password = preferences.getString("mqtt_password", "");
  if (legacyNodeKey != nullptr && legacyNodeKey[0] != '\0') {
    settings.node_name = sanitizeNodeName(preferences.getString("node_name", preferences.getString(legacyNodeKey, "")));
  } else {
    settings.node_name = sanitizeNodeName(preferences.getString("node_name", ""));
  }
  preferences.end();
}

inline void saveNetworkSettings(Preferences& preferences, NetworkSettings& settings) {
  settings.node_name = sanitizeNodeName(settings.node_name);
  preferences.begin("settings", false);
  preferences.putString("wifi_ssid", settings.wifi_ssid);
  preferences.putString("wifi_password", settings.wifi_password);
  preferences.putString("mqtt_server", settings.mqtt_server);
  preferences.putInt("mqtt_port", settings.mqtt_port);
  preferences.putString("mqtt_username", settings.mqtt_username);
  preferences.putString("mqtt_password", settings.mqtt_password);
  preferences.putString("node_name", settings.node_name);
  preferences.end();
}

inline void clearNetworkSettings(Preferences& preferences) {
  preferences.begin("settings", false);
  preferences.clear();
  preferences.end();
}

inline void saveBrokerSettings(Preferences& preferences, const String& mqtt_server, int mqtt_port) {
  preferences.begin("settings", false);
  preferences.putString("mqtt_server", mqtt_server);
  preferences.putInt("mqtt_port", mqtt_port);
  preferences.end();
}

inline bool parseBrokerCommand(const String& command, BrokerEndpoint& endpoint, int fallbackPort = kDefaultMqttPort) {
  int equalsPos = command.indexOf('=');
  if (equalsPos <= 0) return false;

  String brokerStr = command.substring(equalsPos + 1);
  brokerStr.trim();
  if (brokerStr.length() == 0) return false;

  endpoint.host = brokerStr;
  endpoint.port = fallbackPort > 0 ? fallbackPort : kDefaultMqttPort;

  int colonPos = brokerStr.indexOf(':');
  if (colonPos > 0) {
    endpoint.host = brokerStr.substring(0, colonPos);
    String portStr = brokerStr.substring(colonPos + 1);
    int parsedPort = portStr.toInt();
    endpoint.port = parsedPort > 0 ? parsedPort : kDefaultMqttPort;
  }

  endpoint.host.trim();
  return endpoint.host.length() > 0;
}

inline bool isResetCommand(const String& command) {
  return command == "reset" || command == "reset wifi" || command == "reset config";
}

inline bool isBrokerCommand(const String& command) {
  return command.startsWith("broker=") || command.startsWith("mqtt=");
}

}  // namespace FabNodes

#include "FabNodesSignals.h"
#include "FabNodesManifest.h"
#include "FabNodesSerial.h"
#include "FabNodesNet.h"
