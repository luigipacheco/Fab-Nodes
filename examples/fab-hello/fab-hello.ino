// ═══════════════════════════════════════════════════════════════════════════
// FabHello — the button sketch you know, with one wire cut
// ═══════════════════════════════════════════════════════════════════════════
//
// In plain Arduino you'd write:   loop() { pressed = read(); setLed(pressed); }
// That call is a WIRE SOLDERED IN CODE: this button controls this LED,
// decided at compile time, forever.
//
// A FabNode cuts that wire and hands both ends to the network:
//
//   • setLed() stays EXACTLY as you'd write it        (rule 1: functions keep)
//   • each end of the cut wire becomes a SIGNAL:      (rule 2: declare ends)
//       "led"    — the network can change it → runtime runs setLed()
//       "button" — you report it with fab.set() → the network hears changes
//   • "button controls LED" is GONE from the code:    (rule 3: wired outside)
//       you draw that connection outside the firmware — to this pixel,
//       a neighbor's, a relay, a stepper. Rewiring is not reflashing.
//
// loop() shrinks to fab.loop(): the runtime calls your functions when signals
// change. WiFi, MQTT, the setup portal, discovery, and the e-stop are all the
// runtime's job — you never write them. Board: Adafruit QT Py ESP32-S3.

#include <FabNodesRuntime.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

using namespace FabNodes;   // so we can say FAB_BOOL instead of FabNodes::FAB_BOOL

FabNodeRuntime fab;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// ── SIGNALS: the new part — declare what this node shares ───────────────────
// This list IS the node's interface. The hub reads it and builds the UI by
// itself — you configure nothing on the hub side.
FabSignal signals[] = {

  // the network can switch the pixel...           (network → node)
  FAB_SUB("led",   FAB_BOOL,  FAB_CONTROL),

  // ...and set its color, as an [r,g,b] array     (network → node)
  FAB_SUB("color", FAB_ARRAY, FAB_CONTROL),

  // the node reports its button                   (node → network)
  FAB_PUB("button", FAB_BOOL, FAB_PROCESS,
          /*unit*/ "", /*min*/ 0, /*max*/ 1,
          /*send when it changes by ≥*/ 1,
          /*but not more often than (ms)*/ 50,
          /*and at least every (ms)*/ 5000),
};

// ── FUNCTIONS: what happens on a change — the code you'd put in loop() ──────
uint8_t rgb[3] = {255, 255, 255};   // current color (starts white)
bool    lit    = false;             // is the pixel on?

void showPixel() {
  // gamma32() maps raw RGB to what the eye expects — without it, colors on a
  // NeoPixel look pale/washed out because the LED is linear but your eye isn't.
  uint32_t c = pixel.gamma32(pixel.Color(rgb[0], rgb[1], rgb[2]));
  pixel.setPixelColor(0, lit ? c : 0);
  pixel.show();
}

void setLed(bool on) {              // runs when "led" changes — the same
  lit = on;                         // body you'd write in plain Arduino (rule 1)
  showPixel();
}

void setColor(const String& s) {    // runs when "color" changes
  // the payload is text like "[255,80,0]" (or "255,80,0") — read 3 numbers
  int r, g, b;
  if (sscanf(s.c_str(), "[%d,%d,%d]", &r, &g, &b) == 3 ||
      sscanf(s.c_str(),  "%d,%d,%d", &r, &g, &b) == 3) {
    rgb[0] = r;  rgb[1] = g;  rgb[2] = b;
    lit = true;                     // picking a color also turns it on
    showPixel();
  }
}

void safeState() {                  // runs on e-stop or a lost connection:
  setLed(false);                    // outputs off — the core safety rule
}

void readInputs(unsigned long now) {   // runs every loop (now = millis) —
  // the read that lived in loop(), reported instead of acted on (rule 2).
  // Where's setLed(pressed)? Deleted — that wire is drawn outside (rule 3).
  fab.set("button", digitalRead(BUTTON_PIN) == LOW);   // sent only on change
}

// ── setup(): your usual pinMode, then hand the list + functions to fab ──────
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);   // the pixel has its own power pin
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  showPixel();

  FabRuntimeConfig cfg;
  cfg.node_type     = NODE_TYPE;
  cfg.node_name     = NODE_NAME;
  cfg.fw_version    = FW_VERSION;
  cfg.signals       = signals;
  cfg.signal_count  = sizeof(signals) / sizeof(signals[0]);
  cfg.on_safe_state = safeState;
  cfg.on_sample     = readInputs;
  fab.begin(cfg);                   // ← portal, WiFi, MQTT, discovery, e-stop

  fab.onBool("led", setLed);        // read aloud: "when led changes, run setLed"
  fab.onString("color", setColor);  //             "when color changes, run setColor"
}

// ── loop(): the runtime calls your functions for you; just service it ───────
void loop() {
  fab.loop();
}
