# FabNodesCore

FabNodesCore is the shared workflow library for FabNodes sketches in this repo.

Current scope:

- protocol constants such as the setup AP SSID, manifest root, and `system/estop`
- node-name sanitizing
- JSON string escaping for manifest payloads
- default node-name and chip-ID generation
- shared network settings load/save/clear through `Preferences`
- common `broker=` / `mqtt=` serial command parsing
- signal model (`FabSignal`) and manifest builder — protocol `fabnodes/1.1`: chip `id`, `$state` reference, standard `status/safe` + `diag/*` descriptors, enum `options` for string signals (`FAB_SUB_ENUM("mode", FAB_CONTROL, "velocity,move,position")` → manifest `"options":[...]` → selector in the controlling UI; payloads stay strings)
- availability (`FabNodesNet.h`): `fabMqttConnectWithState()` connects with a `$state` Last Will (`offline`, QoS 1, retained) and publishes retained `online`
- latched emergency stop helpers (`FabEstopState`, `fabEstopUpdate`) — `system/estop = 1` forces safe state until an explicit `0`
- retained-control replay guard (`FabControlReplayGuard`) — control topics must never be published retained
- standard diagnostics publisher (`fabServiceDiagnostics`: `diag/rssi`, `diag/ip`, `diag/uptime`)
- mDNS: `fabStartMdns()` plus `.local` broker resolution via `fabApplyBrokerEndpoint()` (default broker: `fabnodes.local`)
- **`FabNodesRuntime.h`** — full node lifecycle (settings, shared captive portal, WiFi/MQTT with fail-safe transitions, e-stop, manifest, diagnostics, serial with custom-command hooks) as a header-only class; include it explicitly (`#include <FabNodesRuntime.h>`), it is not pulled in by `FabNodesCore.h`. Used by every node except fab-knob (custom portal + foreign-topic subscriptions by design).
- **Signal binding layer** (in `FabNodesRuntime.h`) — typed control callbacks and policy publishing driven by the signal metadata, so sketches skip suffix dispatch, payload parsing, and deadband bookkeeping:
  - `fab.onBool/onInt/onFloat/onString(suffix, fn)` — runtime parses the payload, clamps numerics to the signal's declared min/max, validates strings against enum `options` (unknown values dropped with a serial note), then calls the handler. Register after `fab.begin()`; capturing lambdas work (`fab.onBool(sfx[i], [i](bool on){ apply(i, on); })`). Bindings win over `on_control`; unmatched suffixes fall through to it.
  - `fab.set(suffix, value[, decimals])` — publishes honoring the signal's declared `publish_policy`/`deadband`/min-max intervals and formats by declared type (bool → `0`/`1`, int → integer). A publish that doesn't go out (MQTT down) isn't recorded, so the value retries next sample. `fab.signalValue(suffix)` returns the last sampled value (NAN before the first) for displays.
  - Adopters: fab-relay + fab-servo (bindings), fab-sense (`fab.set` replaced its hand-rolled deadband/heartbeat channels).
- **`FabNodesHeater.h`** — reusable closed-loop heater controller (`FabHeater`): PI + time-proportional switching for SSR/MOSFET heaters, with sensor-fault-off, latched overtemp cutoff, and `forceOff()` for safe states. Standalone include, dependency-free. First consumer: fab-struder.
- **`FabNodesDisplay.h`** — shared OLED debug pages for any Adafruit_GFX-compatible display: `fabDrawSetupPage()` (captive-portal instructions) and `fabDrawStatusPage()` (the FabNodes face — happy idle / poop-face busy — wifi icon, name/IP/MQTT/e-stop, value lines). The bitmaps come from the original FabStruder UI and are the shared design language; fab-struder keeps its rich custom page as the flagship example, fab-sense shows the minimal adoption (`SENSE_OLED=1`, ~30 lines). Standalone include; serial debugging needs nothing — every runtime node has `status`/`topics`/`values` built in.

Current adopters (all nodes): fab-led, fab-streamer, fab-struder,
fab-stepper, fab-relay, fab-panel, fab-sense, fab-servo on the full runtime;
fab-knob on the protocol helpers only (custom lifecycle by design).

## Include pattern

The library ships `library.properties` (Arduino) and `library.json`
(PlatformIO). Node projects consume it via `lib_extra_dirs = ../libraries`
in their `platformio.ini`:

```cpp
#include <FabNodesCore.h>
```

## Next candidates for extraction

- common captive-portal save flow
- shared Wi-Fi and MQTT reconnect policy
