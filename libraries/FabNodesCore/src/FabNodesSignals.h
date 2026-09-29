#pragma once

#include <Arduino.h>

namespace FabNodes {

enum FabSignalDir {
  FAB_SIG_PUB,
  FAB_SIG_SUB,
  FAB_SIG_PUBSUB
};

enum FabSignalType {
  FAB_BOOL,
  FAB_INT,
  FAB_FLOAT,
  FAB_STRING,
  FAB_ARRAY
};

enum FabSignalClass {
  FAB_PROCESS,
  FAB_CONTROL,
  FAB_STATUS,
  FAB_DIAGNOSTIC,
  FAB_CONFIG
};

enum FabPublishPolicy {
  FAB_ON_CHANGE,
  FAB_DEADBAND,
  FAB_INTERVAL,
  FAB_ON_CHANGE_HEARTBEAT,
  FAB_STREAM
};

struct FabSignal {
  const char* suffix;
  FabSignalDir dir;
  FabSignalType type;
  FabSignalClass sclass;
  const char* unit;
  bool retained;
  bool has_min;
  float min_value;
  bool has_max;
  float max_value;
  FabPublishPolicy publish_policy;
  float deadband;
  unsigned long min_interval_ms;
  unsigned long max_interval_ms;
  bool enabled_by_default;
  bool visible_by_default;
  // Optional enum options for string signals, comma-separated
  // (e.g. "velocity,move,position"). Emitted in the manifest as
  // "options":[...] so tools render a selector. Payloads stay the
  // human-readable strings; appended after the 16 base fields so older
  // aggregate initializers keep working (value-initialized to nullptr).
  const char* options;
  // v1.2 command hold (sub signals only). When > 0, the node expects a fresh
  // value on this signal at least every hold_ms once it has received one;
  // silence beyond that means whoever was driving it is gone, and the node
  // enters its safe state. 0 = no hold. Appended last (value-initialized).
  unsigned long hold_ms;
};

struct FabSignalRuntime {
  union {
    float f;
    int32_t i;
  } current = {0};
  union {
    float f;
    int32_t i;
  } last_published = {0};
  String str_value = "";
  unsigned long last_sample_ms = 0;
  unsigned long last_change_ms = 0;
  unsigned long last_publish_ms = 0;
  uint32_t publish_count = 0;
  bool dirty = false;
  bool healthy = true;
  bool stale = false;
  // v1.2 hold bookkeeping for sub signals: millis() of the last accepted
  // control value (0 = none since connect) and whether the hold has tripped.
  unsigned long last_control_ms = 0;
  bool held = false;
};

inline const char* fabSignalDirName(FabSignalDir dir) {
  switch (dir) {
    case FAB_SIG_PUB: return "pub";
    case FAB_SIG_SUB: return "sub";
    case FAB_SIG_PUBSUB: return "pubsub";
    default: return "pub";
  }
}

inline const char* fabSignalTypeName(FabSignalType type) {
  switch (type) {
    case FAB_BOOL: return "bool";
    case FAB_INT: return "int";
    case FAB_FLOAT: return "float";
    case FAB_STRING: return "string";
    case FAB_ARRAY: return "array";
    default: return "string";
  }
}

inline bool fabSignalSupportsNumericBounds(FabSignalType type) {
  return type == FAB_INT || type == FAB_FLOAT;
}

inline String fabSignalFullTopic(const String& nodeName, const FabSignal& signal) {
  return nodeName + "/" + String(signal.suffix);
}

// True when value is one of the signal's comma-separated enum options.
// Signals without options accept everything.
inline bool fabOptionAllowed(const FabSignal& signal, const String& value) {
  if (!signal.options || !signal.options[0]) return true;
  const char* p = signal.options;
  while (*p) {
    const char* comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    if (value.length() == len && strncmp(value.c_str(), p, len) == 0) return true;
    if (!comma) break;
    p = comma + 1;
  }
  return false;
}

// Decide whether a new numeric value should go out, per the signal's declared
// publish policy. Pure decision — the caller records the publish into rt on
// success (so a failed MQTT publish retries on the next sample).
inline bool fabPublishDue(const FabSignal& s, const FabSignalRuntime& rt,
                          float value, unsigned long now) {
  if (rt.publish_count == 0) return true;  // first value always goes out
  unsigned long since = now - rt.last_publish_ms;
  if (s.min_interval_ms > 0 && since < s.min_interval_ms) return false;
  float delta = fabsf(value - rt.last_published.f);
  bool changed = s.deadband > 0 ? (delta >= s.deadband) : (delta != 0);
  bool heartbeat = s.max_interval_ms > 0 && since >= s.max_interval_ms;
  switch (s.publish_policy) {
    case FAB_ON_CHANGE: return changed;
    case FAB_DEADBAND: return changed;
    case FAB_INTERVAL: return heartbeat;
    case FAB_ON_CHANGE_HEARTBEAT: return changed || heartbeat;
    case FAB_STREAM: return true;  // rate-limited by min_interval above
  }
  return changed;
}

// Same decision for string signals (last published string lives in rt.str_value).
inline bool fabPublishDueStr(const FabSignal& s, const FabSignalRuntime& rt,
                             const String& value, unsigned long now) {
  if (rt.publish_count == 0) return true;
  unsigned long since = now - rt.last_publish_ms;
  if (s.min_interval_ms > 0 && since < s.min_interval_ms) return false;
  bool changed = value != rt.str_value;
  bool heartbeat = s.max_interval_ms > 0 && since >= s.max_interval_ms;
  switch (s.publish_policy) {
    case FAB_INTERVAL: return heartbeat;
    case FAB_ON_CHANGE_HEARTBEAT: return changed || heartbeat;
    case FAB_STREAM: return true;
    default: return changed;
  }
}

}  // namespace FabNodes

#define FAB_PUB(sfx, typ, cls, unit, lo, hi, db, minMs, maxMs) \
  { sfx, FabNodes::FAB_SIG_PUB, typ, cls, unit, false, \
    true, lo, true, hi, FabNodes::FAB_ON_CHANGE_HEARTBEAT, db, minMs, maxMs, true, true }

#define FAB_SUB(sfx, typ, cls) \
  { sfx, FabNodes::FAB_SIG_SUB, typ, cls, "", false, \
    false, 0, false, 0, FabNodes::FAB_ON_CHANGE, 0, 0, 0, true, true }

#define FAB_SUB_RANGE(sfx, typ, cls, unit, lo, hi) \
  { sfx, FabNodes::FAB_SIG_SUB, typ, cls, unit, false, \
    true, lo, true, hi, FabNodes::FAB_ON_CHANGE, 0, 0, 0, true, true }

// String control with a fixed option set (comma-separated), e.g.
// FAB_SUB_ENUM("mode", FabNodes::FAB_CONTROL, "velocity,move,position")
#define FAB_SUB_ENUM(sfx, cls, opts) \
  { sfx, FabNodes::FAB_SIG_SUB, FabNodes::FAB_STRING, cls, "", false, \
    false, 0, false, 0, FabNodes::FAB_ON_CHANGE, 0, 0, 0, true, true, opts }
