# FabNodesCore

**Version:** 0.3.0 · **Protocol:** fabnodes/1.1 · **License:** MIT

An ESP32 library for building MQTT-connected sensors and actuators. Define
signals and hardware callbacks; the runtime handles setup, connectivity,
discovery and diagnostics.

## Get started

Use [fab-hello](../../examples/fab-hello/TUTORIAL.md) as a working template.
Install the library with Arduino, or point PlatformIO's `lib_extra_dirs` at
the directory containing `FabNodesCore`.

```cpp
#include <FabNodesCore.h>
#include <FabNodesRuntime.h>
```

## Features

- Captive-portal WiFi/MQTT setup with settings saved in ESP32 Preferences.
- Retained manifests describing each signal's topic, type, direction and range.
- Online/offline availability through MQTT Last Will, plus diagnostic heartbeats.
- Latched `system/estop` handling and a callback for safe output states on
  connectivity loss. This is a software stop, not a safety-rated function.
- Typed control bindings, range checks and enum validation.
- Publishing policies with deadbands and minimum/maximum intervals.
- mDNS naming and broker discovery, plus serial setup/debug commands.

## API guide

| API | Use |
|---|---|
| `FabSignal` | Describe inputs, outputs, units, ranges and publishing policies |
| `fab.begin(config)` / `fab.loop()` | Start and service the node runtime |
| `fab.onBool/onInt/onFloat/onString(suffix, callback)` | Handle typed control values; register after `begin()` |
| `fab.set(suffix, value)` | Publish a value using the signal's declared policy |
| `fab.signalValue(suffix)` | Read the last sampled value for a display |
| `on_safe_state` callback | Put hardware outputs into their defined safe state |

`FabNodesHeater.h` provides PI heater control with sensor-fault and overtemperature
handling. `FabNodesDisplay.h` provides setup/status pages for Adafruit_GFX-compatible
displays. Include these helpers when your hardware needs them.

See the [protocol reference](../../README.md) for topics, payloads and manifests.
