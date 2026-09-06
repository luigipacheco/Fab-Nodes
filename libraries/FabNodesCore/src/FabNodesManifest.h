#pragma once

#include <Arduino.h>
#include "FabNodesSignals.h"

namespace FabNodes {

struct FabManifestInfo {
  String node_name = "";
  const char* node_type = "";
  const char* firmware_version = "";
  const char* protocol_version = "fabnodes/1.1";
  String ip_address = "";
  String chip_id = "";
  bool include_state = true;        // advertise <nodeName>/$state availability topic
  bool include_safe_status = true;  // advertise <nodeName>/status/safe
  bool include_diagnostics = true;  // advertise diag/rssi, diag/ip, diag/uptime
};

inline String fabManifestInfoTopic(const String& nodeName, const char* infoPath = "/$info") {
  return nodeName + String(infoPath);
}

inline String fabManifestMirrorTopic(const String& nodeName, const char* manifestRoot = "fabnodes/manifest") {
  return String(manifestRoot) + "/" + nodeName;
}

inline void fabManifestAppendSignal(String& payload, const String& nodeName, const FabSignal& signal) {
  payload += "{\"topic\":\"";
  payload += jsonEscape(fabSignalFullTopic(nodeName, signal));
  payload += "\",\"dir\":\"";
  payload += fabSignalDirName(signal.dir);
  payload += "\",\"type\":\"";
  payload += fabSignalTypeName(signal.type);
  payload += "\"";

  if (signal.unit != nullptr && signal.unit[0] != '\0') {
    payload += ",\"unit\":\"";
    payload += jsonEscape(String(signal.unit));
    payload += "\"";
  }

  if (signal.options != nullptr && signal.options[0] != '\0') {
    payload += ",\"options\":[";
    String opts(signal.options);
    int start = 0;
    bool first = true;
    while (start <= (int)opts.length()) {
      int comma = opts.indexOf(',', start);
      String opt = comma < 0 ? opts.substring(start) : opts.substring(start, comma);
      opt.trim();
      if (opt.length()) {
        if (!first) payload += ",";
        payload += "\"";
        payload += jsonEscape(opt);
        payload += "\"";
        first = false;
      }
      if (comma < 0) break;
      start = comma + 1;
    }
    payload += "]";
  }

  if (fabSignalSupportsNumericBounds(signal.type) && signal.has_min) {
    payload += ",\"min\":";
    payload += String(signal.min_value, signal.type == FAB_INT ? 0 : 3);
  }

  if (fabSignalSupportsNumericBounds(signal.type) && signal.has_max) {
    payload += ",\"max\":";
    payload += String(signal.max_value, signal.type == FAB_INT ? 0 : 3);
  }

  payload += "}";
}

// Standard v1.1 descriptors appended after the node's own signals.
inline void fabManifestAppendStandardSignals(String& payload, const FabManifestInfo& info, bool haveSignals) {
  const FabSignal kSafeSignal =
    { "status/safe", FAB_SIG_PUB, FAB_BOOL, FAB_STATUS, "", true,
      false, 0, false, 0, FAB_ON_CHANGE, 0, 0, 0, true, true };
  const FabSignal kRssiSignal =
    { "diag/rssi", FAB_SIG_PUB, FAB_INT, FAB_DIAGNOSTIC, "dBm", false,
      true, -120, true, 0, FAB_INTERVAL, 0, 15000, 15000, true, true };
  const FabSignal kIpSignal =
    { "diag/ip", FAB_SIG_PUB, FAB_STRING, FAB_DIAGNOSTIC, "", false,
      false, 0, false, 0, FAB_INTERVAL, 0, 15000, 15000, true, true };
  const FabSignal kUptimeSignal =
    { "diag/uptime", FAB_SIG_PUB, FAB_INT, FAB_DIAGNOSTIC, "s", false,
      false, 0, false, 0, FAB_INTERVAL, 0, 15000, 15000, true, true };

  bool first = !haveSignals;
  if (info.include_safe_status) {
    if (!first) payload += ",";
    fabManifestAppendSignal(payload, info.node_name, kSafeSignal);
    first = false;
  }
  if (info.include_diagnostics) {
    if (!first) payload += ",";
    fabManifestAppendSignal(payload, info.node_name, kRssiSignal);
    payload += ",";
    fabManifestAppendSignal(payload, info.node_name, kIpSignal);
    payload += ",";
    fabManifestAppendSignal(payload, info.node_name, kUptimeSignal);
  }
}

inline String fabBuildManifestPayload(const FabManifestInfo& info, const FabSignal* signals, size_t signalCount) {
  String payload = "{\"nodeName\":\"";
  payload += jsonEscape(info.node_name);
  payload += "\",\"nodeType\":\"";
  payload += jsonEscape(String(info.node_type != nullptr ? info.node_type : ""));
  payload += "\",\"fw\":\"";
  payload += jsonEscape(String(info.firmware_version != nullptr ? info.firmware_version : ""));
  payload += "\",\"protocol\":\"";
  payload += jsonEscape(String(info.protocol_version != nullptr ? info.protocol_version : ""));
  payload += "\"";

  if (info.chip_id.length() > 0) {
    payload += ",\"id\":\"";
    payload += jsonEscape(info.chip_id);
    payload += "\"";
  }

  if (info.include_state) {
    payload += ",\"state\":\"";
    payload += jsonEscape(info.node_name + "/$state");
    payload += "\"";
  }

  if (info.ip_address.length() > 0) {
    payload += ",\"ip\":\"";
    payload += jsonEscape(info.ip_address);
    payload += "\"";
  }

  payload += ",\"signals\":[";
  for (size_t i = 0; i < signalCount; i++) {
    if (i > 0) payload += ",";
    fabManifestAppendSignal(payload, info.node_name, signals[i]);
  }
  fabManifestAppendStandardSignals(payload, info, signalCount > 0);
  payload += "]}";
  return payload;
}

}  // namespace FabNodes
