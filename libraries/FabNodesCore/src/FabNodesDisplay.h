#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "FabNodesCore.h"  // fabSetupApSsid() on the setup page

// FabNodesDisplay — optional shared OLED debug pages for any node with an
// Adafruit_GFX-compatible display (SSD1327, SSD1306, LCDs, ...).
//
// Standalone include (not pulled in by FabNodesCore.h) so headless nodes
// carry no display dependency. The node owns the display driver and flushes
// it (e.g. display.display()) after drawing; this header only renders.
//
// Two ready-made pages, generalized from the FabStruder UI:
//   fabDrawSetupPage(gfx, "FabSense", color, w, h, fab.nodeName())
//                                                 — captive-portal instructions
//   fabDrawStatusPage(gfx, info, color)           — face + wifi + state + values
//
// The faces are part of the FabNodes personality: happy face = idle/ok,
// poop face = working hard (info.busy). Nodes with richer UIs (FabStruder)
// reuse the shared bitmaps and draw their own layout.

namespace FabNodes {

// ── Shared bitmap assets (from the original FabStruder UI) ──────────────────
static const unsigned char PROGMEM kFabBmpWifiOk[] = {0x01,0xf0,0x00,0x07,0xfc,0x00,0x1e,0x0f,0x00,0x39,0xf3,0x80,0x77,0xfd,0xc0,0xef,0x1e,0xe0,0x5c,0xe7,0x40,0x3b,0xfb,0x80,0x17,0x1d,0x00,0x0e,0xee,0x00,0x05,0xf4,0x00,0x03,0xb8,0x00,0x01,0x50,0x00,0x00,0xe0,0x00,0x00,0x40,0x00,0x00,0x00,0x00};  // 19x16
static const unsigned char PROGMEM kFabBmpWifiOff[] = {0x21,0xf0,0x00,0x16,0x0c,0x00,0x08,0x03,0x00,0x25,0xf0,0x80,0x42,0x0c,0x40,0x89,0x02,0x20,0x10,0xa1,0x00,0x23,0x58,0x80,0x04,0x24,0x00,0x08,0x52,0x00,0x01,0xa8,0x00,0x02,0x04,0x00,0x00,0x42,0x00,0x00,0xa1,0x00,0x00,0x40,0x80,0x00,0x00,0x00};  // 19x16
static const unsigned char PROGMEM kFabBmpFaceHappy[] = {0x00,0x00,0x00,0x00,0x3c,0x00,0x01,0xe0,0x7a,0x00,0x03,0xd0,0x7e,0x00,0x03,0xf0,0x7e,0x00,0x03,0xf0,0x7e,0x00,0x03,0xf0,0x3c,0x00,0x01,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40,0x00,0x00,0x10,0x40,0x00,0x00,0x10,0x40,0x00,0x00,0x08,0x80,0x00,0x00,0x07,0x00,0x00};  // 29x14
static const unsigned char PROGMEM kFabBmpFacePoop[] = {0x00,0x00,0x00,0x00,0x03,0x00,0x06,0x00,0x31,0x80,0x0c,0x60,0x18,0x80,0x08,0xc0,0x0c,0x00,0x01,0x80,0x1e,0x00,0x03,0xc0,0x3c,0x00,0x01,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x07,0x0d,0x87,0x00,0x01,0x18,0xc4,0x00,0x01,0x0d,0x84,0x00,0x00,0x07,0x00,0x00};  // 29x14

// ── Status page ──────────────────────────────────────────────────────────────
struct FabDisplayInfo {
  String node_name = "";
  String ip = "";
  bool wifi_ok = false;
  bool mqtt_ok = false;
  bool estop = false;
  bool busy = false;          // true -> poop face (node is working hard)
  const char* fw = "";
  // Up to three free-form value lines, e.g. "temp 203.4 C"
  String line1 = "";
  String line2 = "";
  String line3 = "";
};

inline void fabDrawStatusPage(Adafruit_GFX& gfx, const FabDisplayInfo& info,
                              uint16_t color, int16_t w = 128, int16_t h = 128) {
  gfx.fillScreen(0);
  gfx.setTextColor(color);
  gfx.setTextSize(1);

  // Face top-center, wifi icon top-right (the FabNodes header)
  gfx.drawBitmap((w - 29) / 2 - 10, 2, info.busy ? kFabBmpFacePoop : kFabBmpFaceHappy, 29, 14, color);
  gfx.drawBitmap(w - 21, 0, info.wifi_ok ? kFabBmpWifiOk : kFabBmpWifiOff, 19, 16, color);
  gfx.drawLine(0, 18, w, 18, color);

  int16_t y = 24;
  gfx.setCursor(2, y);
  gfx.print(info.node_name);
  y += 12;

  gfx.setCursor(2, y);
  gfx.print(info.wifi_ok ? info.ip : String("no wifi"));
  y += 12;

  gfx.setCursor(2, y);
  if (info.estop) {
    gfx.print("!! E-STOP LATCHED !!");
  } else {
    gfx.print(info.mqtt_ok ? "mqtt ok" : "mqtt down");
  }
  y += 14;

  if (info.line1.length()) { gfx.setCursor(2, y); gfx.print(info.line1); y += 12; }
  if (info.line2.length()) { gfx.setCursor(2, y); gfx.print(info.line2); y += 12; }
  if (info.line3.length()) { gfx.setCursor(2, y); gfx.print(info.line3); y += 12; }

  // Footer
  if (h >= 64) {
    gfx.drawLine(0, h - 14, w, h - 14, color);
    gfx.setCursor(2, h - 10);
    gfx.print("FabNodes ");
    gfx.print(info.fw);
  }
}

// ── Setup-mode page (captive-portal instructions) ────────────────────────────
// node_name should be the runtime's nodeName() so the screen shows the same
// AP the device is actually broadcasting; empty falls back to the chip id.
inline void fabDrawSetupPage(Adafruit_GFX& gfx, const char* title,
                             uint16_t color, int16_t w = 128, int16_t h = 128,
                             const String& node_name = String()) {
  gfx.fillScreen(0);
  gfx.setTextColor(color);

  gfx.setTextSize(2);
  gfx.setCursor(10, 0);
  gfx.print("SETUP");
  gfx.drawLine(0, 20, w, 20, color);

  gfx.setTextSize(1);
  int16_t y = 26;
  gfx.setCursor(5, y);       gfx.print("WiFi:");
  gfx.setCursor(5, y + 10);  gfx.print(fabSetupApSsid(node_name));
  y += 26;
  gfx.setCursor(5, y);       gfx.print("Visit:");
  gfx.setCursor(5, y + 10);  gfx.print("192.168.4.1");

  if (h >= 64) {
    gfx.drawLine(0, h - 16, w, h - 16, color);
    gfx.setCursor(5, h - 12);
    gfx.print(title);
  }
}

}  // namespace FabNodes
