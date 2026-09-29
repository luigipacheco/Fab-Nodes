# FabNodes — agent briefing

"Home Assistant for robotics": ESP32 nodes announce themselves over MQTT with
retained manifests; anything that speaks MQTT can discover, control, and wire
them. Keep everything **minimal and efficient** — prototype posture, but
structural decisions (safety, protocol) are made deliberately. Protocol spec:
`README.md`.

This repo is the open core: the shared runtime and one complete example node.
Individual production node firmware lives in separate repositories.

## Map

- `libraries/FabNodesCore/` — the shared library, consumed via
  `lib_extra_dirs`. Key headers: `FabNodesRuntime.h` (full lifecycle: a node =
  pins + FabSignal[] + callbacks; typed bindings `fab.onBool/onInt/onFloat/
  onString` + policy publishing `fab.set()` — prefer these over hand-rolled
  dispatch/deadband code), `FabNodesHeater.h` (closed-loop heater),
  `FabNodesDisplay.h` (shared OLED pages).
- `examples/fab-hello/` — annotated tutorial node + TUTORIAL.md; the copy-me
  template for new node types. Nothing depends on it, so it is free to stay
  maximally readable: it is teaching material first, firmware second.

## Protocol rules (fabnodes/1.2 — don't casually violate)

- Flat readable topics `<nodeName>/<suffix>`; raw payloads (no JSON objects
  in controls); arrays as JSON brackets `[[r,g,b],...]`
- Control topics are NEVER retained; nodes drop retained replays
- `system/estop` = 1 latches every node safe until explicit 0 (retained, QoS 1)
- Nodes fail safe on WiFi/MQTT loss — that's the core safety property
- `$state` online/offline via LWT; UIs also require diag heartbeats (15 s)
  before trusting "online" (stale after 45 s)
- Enum string signals declare `options` in the manifest (`FAB_SUB_ENUM`);
  payloads stay strings like "velocity", never numeric codes
- Command hold (v1.2): a `sub` signal with `hold_ms` goes safe when quiet;
  publishers must re-send at least every `hold_ms/3`. Off by default;
  per-signal in firmware, node-wide in the portal
- Setup mode is non-terminal (AP+STA, background retry, auto-exit) — never
  make a code path strand a node in AP mode
- v1.x changes must be additive — old nodes keep working

## Conventions

- Naming: products FabThing / slugs fab-thing
- PlatformIO pinned to espressif32@^6.9.0 (Arduino core 2.x, C++11 — no
  NSDMI in aggregates, append struct fields at the end)
- Compile-verify firmware (`pio run`) before committing
- `libraries/FabNodesCore.zip` is the Arduino IDE install path — regenerate it
  when the library's headers change
- Commit messages: what + why, no fluff
