# FabNodes Core
## Raw Scalar + Array MQTT Architecture

FabNodes are modular hardware nodes designed to expose direct, human-readable hardware functionality over MQTT with minimal firmware complexity.

High-level logic, scaling, mapping, and orchestration are handled externally (MQTTouch, Blender, automation systems).

**Version:** 0.3.0 · **Protocol:** fabnodes/1.1 · **License:** MIT

Build an ESP32 node with the FabNodesCore library and the
[fab-hello example](examples/fab-hello/TUTORIAL.md). The runtime handles setup,
WiFi/MQTT connections, device discovery, diagnostics and software stop.

---

# 0. Quick Start

A FabNode needs two things: an MQTT broker, and a node. This repo is the node
side — [`examples/fab-hello`](examples/fab-hello/), a complete working node in
one annotated file, plus [`FabNodesCore`](libraries/FabNodesCore/), the runtime
it stands on. Any MQTT 3.1.1 broker works; Mosquitto is the usual choice.

**1. Run a broker** (skip if you already have one):

```bash
docker run -it -p 1883:1883 eclipse-mosquitto:2 mosquitto -c /mosquitto-no-auth.conf
```

**2. Flash the example** onto an Adafruit QT Py ESP32-S3 (or any ESP32 — see
§10.1):

```bash
pip install platformio
cd examples/fab-hello
pio run -t upload
```

**3. Point it at your broker.** The node boots into setup mode and broadcasts a
WiFi network named `fabnodes-<node name>`. Join it, and the setup page opens by
itself (or browse to `192.168.4.1`): fill in your WiFi, the broker address, and
a node name. Save, and it joins the network.

**4. Watch it announce itself:**

```bash
mosquitto_sub -h localhost -t '#' -v
```

The retained `<nodeName>/$info` message is the node describing its own signals —
that manifest is how any UI builds a control surface without being told
anything. Everything else on the wire is live data you can read and type by
hand:

```bash
mosquitto_pub -h localhost -t 'fab-hello1/led' -m 1
mosquitto_pub -h localhost -t 'fab-hello1/color' -m '[255,0,0]'
```

[examples/fab-hello/TUTORIAL.md](examples/fab-hello/TUTORIAL.md) walks this
whole path in detail and is the template for writing your own node.

Orchestration lives outside the firmware by design (§9) — dashboards, dataflow
editors, Blender, or your own script, anything that speaks MQTT can drive a
FabNode.

---

# 1. Core Principles

- Topics are flat and readable  
- Each topic represents one functional signal  
- Payloads are raw values (no JSON objects for control topics)  
- Scalars are preferred  
- Arrays are allowed when hardware naturally requires grouped values  
- Direction (publish/subscribe) is defined in `$info`  
- Firmware remains hardware-near and minimal  

---

# 2. Topic Structure

<nodeName>/<signal>  
<nodeName>/<function>/<signal>

### Examples

extruderA/enable  
extruderA/feed/speed  
extruderA/fan/speed  
extruderA/zone1/tempTarget  
extruderA/zone1/tempCurrent  
ledStrip1/colors  

- `<nodeName>` is defined in setup mode and must be unique on the network  
- `<nodeName>` should use lowercase `a-z`, `0-9`, `_`, `-` only (max 32 chars recommended)  
- `nodeType` identifies the hardware class (e.g. `extruder`, `ix-knob`, `ledStrip`) and does not need to be unique  
- `<function>` is optional grouping  
- `<signal>` represents a hardware capability  

---

# 3. Payload Format

## 3.1 Scalar Types

| Type   | Representation | Example |
|--------|---------------|---------|
| BOOL   | 0 or 1       | 1 |
| INT    | integer      | 120 |
| FLOAT  | decimal      | 201.5 |
| STRING | plain text   | rpm |

---

## 3.2 Array Support

Arrays are allowed for grouped hardware data such as:

- LED strips  
- RGB values  
- Matrices  
- Multi-channel outputs  
- Sensor bundles  

### Array Format

[1,2,3]

Nested arrays are allowed:

[[255,0,0],[0,255,0],[0,0,255]]

### Example: LED Strip

Topic:  
ledStrip1/colors  

Payload:  
[[255,0,0],[0,255,0],[0,0,255]]

### Example: Relay Bank

Topic:  
multiRelay1/states  

Payload:  
[1,0,1,1]

---

# 4. Control Payload Rules

- No JSON objects in control topics  
- Raw numbers, strings, or arrays only  
- Whitespace must be trimmed  
- Numeric values must be clamped to allowed ranges  
- BOOL values:
  - 0 = false  
  - non-zero = true (accepted input), but nodes should normalize published BOOL values to `0` or `1`  

---

# 5. Node Manifest ($info)

Each FabNode publishes a retained manifest describing its capabilities.

Canonical topic:

<nodeName>/$info

Optional shared discovery mirror (retained):

fabnodes/manifest/<nodeName>

Example:

fab-struder1/$info

Example payload (JSON allowed here):

{
  "nodeName": "fab-struder1",
  "nodeType": "fab-struder",
  "fw": "0.2.0",
  "protocol": "fabnodes/1.1",
  "id": "a1b2c3d4",
  "state": "fab-struder1/$state",
  "signals": [
    {"topic":"fab-struder1/enable","dir":"sub","dtype":"bool"},
    {"topic":"fab-struder1/feed/speed","dir":"sub","dtype":"int","min":0,"max":500},
    {"topic":"fab-struder1/zone1/tempCurrent","dir":"pub","dtype":"float"},
    {"topic":"fab-led1/colors","dir":"sub","dtype":"array","subtype":"int"}
  ]
}

### Manifest Fields

| Field | Meaning |
|-------|--------|
| nodeName | unique node name used as the MQTT topic prefix |
| nodeType | hardware class / capability family for the node |
| protocol | protocol version string (example: `fabnodes/1.1`) |
| id | chip ID (v1.1) — stable hardware identity, used to detect nodeName collisions |
| state | availability topic reference (v1.1), e.g. `fab-struder1/$state` |
| dir | "sub" = node subscribes (actuator), "pub" = node publishes (sensor) |
| dtype | bool, int, float, string, array |
| subtype | optional element type inside array |
| min, max | optional scalar limits |
| options | (v1.1) accepted values for a string signal, e.g. `["velocity","move","position"]` — payloads stay the human-readable strings; tools render a selector. Declared in firmware via `FAB_SUB_ENUM("mode", FAB_CONTROL, "velocity,move,position")` |

---

# 6. Example Node Definitions

Illustrations of the topic layout for typical node types — what the protocol
looks like in practice, not firmware shipped here.

## Fab-Struder Node

Subscribes:

fab-struder1/enable
fab-struder1/feed/speed
fab-struder1/fan/speed
fab-struder1/zone1/tempTarget

Publishes:

fab-struder1/zone1/tempCurrent

---

## Fab-Stepper Node

fab-stepper1/enable
fab-stepper1/direction
fab-stepper1/mode
fab-stepper1/speed
fab-stepper1/distance

- direction = 1 -> forward
- direction = 0 -> reverse
Firmware logic maps directly to hardware control without high-level computation.

---

## Fab-Led Node

fab-led1/colors

Payload example:

[[255,0,0],[0,255,0],[0,0,255]]

---

# 7. Setup Mode & Network Management

Each FabNode supports runtime configuration (no firmware reflash required).

## 7.1 Configurable Parameters

- WiFi SSID + password
- MQTT broker address + port
- MQTT username + password (optional)
- `nodeName` — unique node identifier on the network

## 7.2 Boot Sequence

1. Load stored configuration from flash (`Preferences`)
2. If WiFi or MQTT broker is unconfigured → enter setup mode
3. Attempt WiFi connection (30 × 500 ms)
4. If WiFi fails → enter setup mode
5. MQTT connection is attempted in the main loop (non-blocking)
6. On successful MQTT connect:
   - Subscribe to all signal topics
   - Publish `<nodeName>/$info` (retained, canonical manifest)
   - Publish `fabnodes/manifest/<nodeName>` (retained discovery mirror)
   - Enter normal operation

## 7.3 Setup Mode (Captive Portal)

When setup mode is triggered the node starts a WiFi Access Point and a captive portal:

- AP SSID: `fabnodes-<nodeName>`, where `nodeName` is the node name in
  effect — the one saved in flash if the node has been through the portal,
  otherwise `NODE_NAME` from the node's `config.h`. A node with neither falls
  back to `fabnodes-<chipId>`. Truncated to the 32-byte SSID limit, and
  printed on serial and on the OLED setup page where present. Give each board
  its own `NODE_NAME` so a room full of fresh nodes broadcasts distinguishable
  networks.
- AP IP: `192.168.4.1`
- DNS: all domains redirect to `192.168.4.1` (captive portal)
- Web UI at `http://192.168.4.1` — configure WiFi, broker, and node name
- On save: settings written to flash, device restarts

## 7.4 MQTT Failure Recovery

- Reconnect is attempted every 5 s (non-blocking)
- Failures are counted inside a 60 s sliding window
- After **12 failures within 60 s** → safe state + return to setup mode
- On every reconnect attempt: reason code is logged to serial

## 7.5 WiFi Watchdog

- WiFi status is checked every loop iteration
- On disconnect: safe state is entered and `WiFi.reconnect()` is called every 10 s
- WiFi reconnect does **not** re-enter setup mode (broker may be temporarily unreachable)

## 7.6 nodeName Rules

- Stored in flash under key `node_name`
- Must be unique on the network
- Sanitized at save time: lowercase `a-z`, `0-9`, `_`, `-` only (max 32 chars)
- Spaces, dots, and slashes are converted to `_`
- Used as the MQTT topic prefix: `<nodeName>/<signal>`

---

# 8. Safety Behavior

On MQTT broker disconnect or WiFi loss:

- `enable = 0`
- `speed = 0`
- All actuator outputs enter safe/off state immediately

Nodes must not require a broker message to enter safe state — it is the default on any loss of connectivity. **This is the core safety property: a node that cannot hear the network is safe by default.**

## 8.1 Emergency Stop (v1.1, software stop)

Global topic (published QoS 1, **retained**):

system/estop

- `1` → every node forces its local safe state and **latches**: all control messages are ignored
- `0` → the latch clears; controls are accepted again, but outputs stay safe until explicitly commanded
- Because it is retained, nodes that connect late latch immediately

Each node acknowledges its latch state (retained):

<nodeName>/status/safe   → 1 = latched safe, 0 = normal operation

This is a **software stop** over WiFi + MQTT — it makes the safe state immediate, but the guaranteed property remains fail-safe-on-disconnect. Physical emergency stops remain hardware's responsibility.

## 8.2 Retained Control Messages Are Forbidden (v1.1)

Control topics must **never** be published with the MQTT retain flag. A retained control value would be replayed to a rebooting node and could restart motion or heat with nobody at the controls. Nodes drop control messages that arrive in the first moments after subscribing (retained replays are delivered immediately on subscribe). `system/estop` is the single deliberate exception.

---

# 9. Architectural Philosophy

FabNodes expose:

- Direct hardware capabilities  
- Human-readable abstractions  
- Minimal firmware logic  

FabNodes do NOT:

- Compute flow rates  
- Perform orchestration  
- Implement sequencing logic  
- Embed complex control graphs  

Higher abstraction layers (MQTTouch, Blender, automation systems) compose signals into behaviors.

---

# 10. Nodes

## Naming Convention

Everything in the ecosystem follows one scheme:

| Level | Form | Example |
|---|---|---|
| Family / brand | **FabNodes** | FabNodes, FabNodes Hub, FabNodes Green |
| Product / display name | **Fab + Thing** (CamelCase) | FabLed, FabStepper, FabStruder, FabKnob, FabStreamer |
| `nodeType` + repo folder | kebab-case slug of the product name | `fab-led`, `fab-stepper`, `fab-struder` |
| Node instance (`nodeName`) | `<nodeType><n>` by default, user-renameable | `fab-led1`, `fab-stepper-xaxis` |
| Shared code | **FabNodes + Role** | FabNodesCore |

Rules of thumb: the suffix names **what the node does for you** (Led, Knob, Stepper), not the chip or module inside it; one word where possible; portmanteaus welcome when they're memorable (FabStruder). The folder name, `nodeType`, and default name prefix always match exactly.

Each node type can have multiple instances on the network. The **node type** (e.g. `fab-led`) identifies the hardware class, while the **node name** (e.g. `fab-led1`, `fab-led-ceiling`) is set per device during setup and becomes the MQTT topic prefix. Node names must be unique on the network.

## FabHello (`examples/fab-hello/`)

The tutorial node — zero extra hardware (Adafruit QT Py ESP32-S3: BOOT
button in, onboard NeoPixel out). Written as *an Arduino sketch that shares
its I/O* — globals/`setup()`/`loop()` plus one new idea (signals) — and
heavily annotated so the code itself is the tutorial. Start here:
[examples/fab-hello/TUTORIAL.md](examples/fab-hello/TUTORIAL.md) walks from
flash to first signal on the wire; making your own node = copying this folder.

| Signal | Dir | Type | Description |
|--------|-----|------|-------------|
| `<nodeName>/led` | sub | bool | Onboard pixel on/off |
| `<nodeName>/color` | sub | array | `[r,g,b]` pixel color |
| `<nodeName>/button` | pub | bool | BOOT button pressed/released |
| `<nodeName>/$info` | pub | json | Node manifest (retained) |

**FabNodes protocol**: v1.1 — full runtime (portal, `$state`, e-stop, diagnostics, mDNS).


## Shared Library (`libraries/FabNodesCore/`)

`FabNodesCore` is the shared helper layer for keeping FabNodes sketches aligned on workflow, setup UI conventions, protocol constants, settings persistence, and serial/MQTT utility behavior. It covers common settings, string helpers, broker-command parsing, the `FabSignal` model, the manifest builder, and the full v1.1 layer (`$state` + LWT, latched e-stop, replay guard, diagnostics, mDNS).

**`FabNodesRuntime.h`** is the full-lifecycle layer: settings, captive portal (shared HTML), WiFi/MQTT lifecycle with fail-safe transitions, e-stop, manifest, diagnostics, and serial commands (with hooks for custom commands/status/values) in one header-only class. A node sketch is just pins + a `FabSignal[]` + callbacks (`on_control`, `on_safe_state`, `on_sample`) — see [`examples/fab-hello`](examples/fab-hello/) as the template, and `FabNodesRuntime.h` itself for the full set of hooks.

---
---

# 10.1 Building (PlatformIO)

Each node is a PlatformIO project consuming `FabNodesCore` via `lib_extra_dirs`:

```
cd examples/fab-hello
pio run             # build
pio run -t upload   # flash
pio device monitor  # serial console (115200)
```

Notes:

- The example targets the Adafruit QT Py ESP32-S3 (BOOT button in, onboard
  NeoPixel out — no extra parts). Any ESP32 with a spare input and an
  addressable pixel works: change `board` in `platformio.ini` and the pins in
  `config.h`.
- Library versions are pinned in each `platformio.ini`; the platform is pinned
  to `espressif32@^6.9.0` (Arduino core 2.x, C++11).
- **Arduino IDE instead?** Install
  [`libraries/FabNodesCore.zip`](libraries/FabNodesCore.zip) via *Sketch →
  Include Library → Add .ZIP Library*, then open
  `examples/fab-hello/fab-hello.ino`.

## Writing your own node

Copy `examples/fab-hello/` to a new folder, change `NODE_TYPE` / `NODE_NAME` in
`config.h`, and edit the signal list plus the functions behind it:

- **Let the network control something** — 3 lines: a `FAB_SUB` in the signal
  list, a function that does the thing, and a `fab.onBool/onInt/onFloat/onString`
  binding them together.
- **Report something to the network** — 2 lines: a `FAB_PUB` in the signal
  list, and a `fab.set(...)` in `readInputs()`.

Nothing else in the file changes. WiFi, the setup portal, MQTT, discovery,
diagnostics and the e-stop all come from the runtime.

---

# 11. Protocol v1.1 Additions

v1.1 is additive — v1.0 nodes keep working, they just show as "availability unknown".

## 11.1 Availability (`$state`)

Every node publishes a retained availability topic backed by an MQTT Last Will:

<nodeName>/$state

- `online` — published (retained) on every successful MQTT connect
- `offline` — written by the broker via Last Will (QoS 1, retained) when the node drops

This makes the retained manifest trustworthy: tools subscribe to `+/$state` alongside `fabnodes/manifest/#` and gray out dead nodes instantly.

## 11.2 Standard Diagnostics

Published on a fixed interval (default 15 s), advertised in the manifest:

<nodeName>/diag/rssi     → WiFi signal (dBm)
<nodeName>/diag/ip       → current IP address
<nodeName>/diag/uptime   → seconds since boot

## 11.3 Zero-Config Broker (`fabnodes.local`)

The setup portal defaults the broker field to `fabnodes.local`. Nodes resolve `.local` hostnames via mDNS on every reconnect attempt, and advertise themselves as `<nodeName>.local`. Manual IP entry remains available for networks that block multicast.

## 11.4 Enum Options For String Signals

String controls with a fixed set of accepted values declare them in the
manifest so tools can render a selector:

{"topic":"fab-stepper1/mode","dir":"sub","type":"string","options":["velocity","move","position"]}

- Payloads stay the human-readable strings (`velocity`), never numeric codes —
  codes fail silently when the option set changes; strings fail loudly
- Declared in firmware with one macro:
  `FAB_SUB_ENUM("mode", FabNodes::FAB_CONTROL, "velocity,move,position")`
- `options` is descriptive metadata: nodes must still tolerate unknown values
  (ignore them), and tools may offer free-text entry alongside the selector

## 11.5 Canonical Array Format

The canonical wire format for arrays is JSON-style brackets:

- 1-D: `[1,2,3]`
- nested: `[[255,0,0],[0,255,0]]`

The semicolon form (`r,g,b;r,g,b`) is a legacy alias that existing nodes may keep accepting, but tools should emit bracket form only.

---

# 12. Version

FabNodes Protocol v1.1
Raw Scalar + Array Edition

FabNodesCore library: **0.3.0** (Arduino and PlatformIO).

