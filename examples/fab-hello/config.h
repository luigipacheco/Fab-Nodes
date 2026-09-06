#pragma once

// FabHello settings. Target board: Adafruit QT Py ESP32-S3.
// (Its "onboard LED" is a NeoPixel on pin 39, powered by pin 38 — the
//  board's own variant already defines PIN_NEOPIXEL / NEOPIXEL_POWER.)

// This board's name on the network. Give every node its own — it is the
// MQTT topic root, the mDNS host, and the setup AP (fabnodes-{name}).
// Lowercase letters, digits, _ and - only. The portal can override it.
#define NODE_NAME        "helloluigi"
#define NODE_TYPE        "fab-hello"   // node type + default name prefix
#define FW_VERSION       "0.3.0"
#define SERIAL_BAUD_RATE 115200

#define BUTTON_PIN       0    // the BOOT button, on every ESP32
#define LED_BRIGHTNESS   80   // 0-255; the onboard pixel is bright, be kind
