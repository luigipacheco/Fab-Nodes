# Build your first FabNode

In this tutorial you'll flash a node whose **button anyone on the network can
read and whose pixel anyone can control**, watch it announce itself, and wire
it to something else without touching the firmware again — using nothing but
the board and a USB cable. No soldering, no extra parts.

**You need:** an Adafruit QT Py ESP32-S3, a USB-C cable, a computer with
Python, and an MQTT broker reachable from your WiFi. If you don't have one:

```bash
docker run -it -p 1883:1883 eclipse-mosquitto:2 mosquitto -c /mosquitto-no-auth.conf
```

The `mosquitto_sub` / `mosquitto_pub` command-line tools (from the same
Mosquitto project) are used below to watch and poke the node; any MQTT client
works, including a graphical one like MQTT Explorer.

## The idea: cut one wire

You already know how to write this sketch — a button that lights the pixel:

```cpp
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void setLed(bool on) {                    // a function that does something
  pixel.setPixelColor(0, on ? pixel.Color(255, 255, 255) : 0);
  pixel.show();
}

void setup() {
  pinMode(0, INPUT_PULLUP);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixel.begin();
}

void loop() {
  bool pressed = digitalRead(0) == LOW;   // read the input
  setLed(pressed);                        // ← THE WIRE: button→LED, forever
}
```

Look at that last line. `setLed(pressed)` is a **wire soldered in code**: this
button controls this LED, decided at compile time, forever.

Turning this into a FabNode means **cutting that one wire and handing both
ends to the network**. Three translation rules:

1. **Your functions don't change.** `setLed()` above is *identical* in the
   FabNode. Functions are the "do something" — FabNodes keeps them as-is.
2. **Each end of the cut wire becomes a signal.** The LED end: declare
   `FAB_SUB("led")` and tell fab *"when it changes, run setLed"*. The button
   end: declare `FAB_PUB("button")` and report it with
   `fab.set("button", pressed)`.
3. **The connection itself leaves the firmware.** Nothing in the FabNode
   says "button controls LED" anymore — that wire gets drawn outside, by
   whatever is orchestrating the network, and you can just as easily draw it
   to a neighbor's pixel, a relay, or a stepper. Rewiring is no longer
   reflashing.

So `loop() { read; decide; write; }` splits apart:

| In your Arduino sketch | In the FabNode |
|---|---|
| the *write* — `setLed(...)` body | unchanged function, bound with `fab.onBool("led", setLed)` |
| the *read* — `digitalRead(...)` | moves to `readInputs()`, reported with `fab.set("button", ...)` |
| the *decision* — `setLed(pressed)` | **deleted** — drawn outside the firmware instead |
| `loop()` | just `fab.loop()` |

Everything else (WiFi, the setup portal, MQTT, discovery, the e-stop) comes
from the shared runtime — you never write it. Open
[fab-hello.ino](fab-hello.ino) next to this page and find each rule; **the
code is the tutorial**. Every FabNode — a relay, a servo, a stepper, an
extruder — is this same translation, just scaled up. (This node also
declares one signal with no Arduino ancestor — `color`, an `[r,g,b]` array —
because once the pixel is on the network, why not let the network pick the
color.)

## The recipe (this is the part to remember)

Everything you will ever add to a node is one of these two moves:

- **Let the network control something** — 3 lines:
  a `FAB_SUB` in the signal list, a function that does it,
  and a `fab.onBool/onInt/onFloat/onString` binding them together.
- **Report something to the network** — 2 lines:
  a `FAB_PUB` in the signal list, and a `fab.set(...)` in `readInputs()`.

That's it. A relay is the first move four times. A sensor board is the
second move six times. An extruder is both, plus math.

## Step 1 — Flash

```bash
pip install platformio
git clone https://github.com/luigipacheco/Fab-Nodes
cd fab-nodes/examples/fab-hello
pio run -t upload
```

(Upload fails with "port busy" or "device not functioning"? See the last
section — don't hold BOOT during a normal upload.)

## Step 2 — Get it online

1. The node boots into setup mode: it broadcasts a WiFi network named
   **`fabnodes-` + the node name from `config.h`** — out of the box that is
   `fabnodes-fabnodes_helloluigi`. Change `NODE_NAME` in `config.h` before you
   flash and the AP renames with it, so a room full of nodes is telling apart
   at a glance. The serial monitor (`pio device monitor`) prints the AP name
   at boot.
2. Connect to it with your phone or laptop; the setup page opens by itself
   (or browse to `192.168.4.1`).
3. Fill in: your WiFi name/password, the **MQTT server** (an IP like
   `192.168.1.50`, or a `.local` name — the field defaults to
   `fabnodes.local`), the broker username/password if it needs them, and a
   **node name nobody else on the network is using** — your name works:
   `fab-hello-luis`.
4. Save. The node restarts and joins.

## Step 3 — See it on the network

Point a subscriber at your broker and watch everything:

```bash
mosquitto_sub -h <broker> -t '#' -v
```

Your node is already talking. The retained `fab-hello-luis/$info` line is its
**manifest** — the node describing its own signals, generated from the
`SIGNALS` list in your firmware. That is what lets a dashboard build a control
surface for a node it has never seen: nothing is configured on the other side.

Now drive it from the other direction, in a second terminal:

```bash
mosquitto_pub -h <broker> -t 'fab-hello-luis/led' -m 1
mosquitto_pub -h <broker> -t 'fab-hello-luis/color' -m '[255,0,0]'
```

The pixel lights white, then goes red. Notice what you just did: **the payload
is plain readable text** — no app, no encoding, the protocol is something you
can type by hand.

- Hold the BOOT button → watch `button` flip to 1 in your subscriber.
- Latch the **e-stop** → the pixel goes out and stays out. Your `safeState()`
  ran, and the node ignores controls until the stop is explicitly cleared.
  Every FabNode behaves this way:

  ```bash
  mosquitto_pub -h <broker> -t 'system/estop' -m 1 -r -q 1   # everything safe
  mosquitto_pub -h <broker> -t 'system/estop' -m 0 -r -q 1   # release
  ```

- Pull the plug on your broker, or walk the node out of WiFi range → the pixel
  goes out on its own. A node that can't hear the network turns itself off.

## Step 4 — Draw the wire back

Remember the wire you cut in `loop()`. Nothing in the firmware reconnects it,
so reconnect it yourself — from outside. This is the whole idea of FabNodes,
in eight lines:

```python
# pip install paho-mqtt
import paho.mqtt.client as mqtt

NODE = "fab-hello-luis"
c = mqtt.Client()
c.on_connect = lambda c, *_: c.subscribe(NODE + "/button")
c.on_message = lambda c, _, m: c.publish(NODE + "/led", m.payload)
c.connect("<broker>", 1883)
c.loop_forever()
```

Run it: your button is a push-to-light again. But the path now runs
button → broker → script → broker → pixel, and **you own the middle**.

Now change one string — publish to a *neighbor's* node name instead of your
own. Your button drives their pixel. No reflash, no recompile, no rewiring.
That's the point: signals are shared on the network, and behavior is wired,
not compiled.

Anything that speaks MQTT can be that middle — a script like this one,
Node-RED, Home Assistant, Blender, a visual dataflow editor, a spreadsheet
with an MQTT plugin. The node neither knows nor cares.

## Step 5 — Make it yours

Open `fab-hello.ino` and change one thing at a time (`pio run -t upload`
after each):

- **Analog input**: a potentiometer (or nothing — a floating pin makes a
  noisy "sensor") on GPIO 34:

  ```cpp
  // in SIGNALS:
  FAB_PUB("knob", FabNodes::FAB_FLOAT, FabNodes::FAB_PROCESS, "", 0, 100, 0.5, 100, 5000),
  // in readInputs():
  fab.set("knob", analogRead(A0) * 100.0f / 4095.0f, 1);
  ```

  Reflash and re-read `$info`: the manifest now carries a `knob` float with
  its range and deadband, and your subscriber starts seeing values. You never
  told anything else on the network that the signal exists.

- **More outputs**: add another `FAB_SUB` + `fab.onBool(...)` driving another
  pin.
- **Real hardware**: a relay board, a servo, a stepper driver, a sensor over
  I2C — every one of them is the same three sections as this file, just more
  of each. `FabNodesRuntime.h` in the shared library lists every hook
  available to you; `FabNodesHeater.h` shows what a closed-loop one looks
  like.
- **Your own node**: copy this folder
  (`cp -r examples/fab-hello fab-yourthing`), point `lib_extra_dirs` in
  `platformio.ini` at wherever `libraries/` now sits relative to you, rename
  `NODE_TYPE` and `NODE_NAME` in `config.h`, then change the signals +
  functions. That's the whole procedure.

## If something misbehaves

- `pio device monitor` at 115200 — the node narrates everything it does.
  Type `status` for a summary, `topics` for its MQTT topics, `help` for
  the rest, `reset` to wipe settings and reopen the setup portal.
- Node not showing up on the broker? Check the serial monitor first — it
  prints every connection attempt and the exact topics it publishes.
- Two people picked the same node name → both nodes fight over the same
  topics. Pick a unique name in the portal (serial `reset` gets you back
  there).
- **Upload fails with "port busy" / "device is not functioning" (Windows)**
  — boards like the QT Py have no auto-reset chip; PlatformIO resets them
  in *software* by signaling the already-running firmware. `BUTTON_PIN` is
  GPIO 0, which is also the chip's boot-strap pin — if it's held down at
  the exact moment of reset, the board drops into its raw bootloader
  instead of your firmware, and the next upload has nothing to talk to.
  Fix: unplug, hold **BOOT**, plug back in while still holding it, release,
  then upload again. Once your firmware is running, pressing the button
  during normal use is completely safe — this only matters at the instant
  of reset. **Rule of thumb: don't hold the button while plugging in or
  uploading.**

## Safety, in one paragraph

`safeState()` runs when the e-stop latches **and** whenever the node loses
WiFi or the broker. A FabNode that can't hear the network turns itself off —
that's the core safety property, and your tiny node already has it. The
software e-stop is a convenience layer on top, never a substitute for a
physical cutoff on dangerous hardware.
