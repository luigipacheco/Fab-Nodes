#pragma once

#include <Arduino.h>

namespace FabNodes {

typedef void (*FabSerialPrintFn)();
typedef bool (*FabSerialCustomCommandFn)(const String& command);

struct FabSerialStatusSnapshot {
  const char* title = "FabNode";
  String node_name = "";
  const char* node_type = "";
  const char* protocol_version = "";
  bool wifi_connected = false;
  String wifi_ssid = "";
  String wifi_ip = "";
  bool mqtt_connected = false;
  String mqtt_server = "";
  int mqtt_port = kDefaultMqttPort;
  size_t signal_count = 0;
  size_t digital_signal_count = 0;
  size_t analog_signal_count = 0;
  bool setup_mode = false;
};

struct FabSerialHandlers {
  FabSerialPrintFn print_topics = nullptr;
  FabSerialPrintFn print_values = nullptr;
  FabSerialPrintFn print_custom_status = nullptr;
  FabSerialPrintFn print_custom_help = nullptr;
  FabSerialCustomCommandFn handle_custom_command = nullptr;
};

struct FabSerialCommandResult {
  bool handled = false;
  bool reset_requested = false;
  bool broker_command_received = false;
  bool broker_update_requested = false;
  BrokerEndpoint broker_endpoint;
};

inline void fabPrintDefaultStatus(const FabSerialStatusSnapshot& snapshot, const FabSerialHandlers& handlers) {
  Serial.printf("\n=== %s Status ===\n", snapshot.title);
  Serial.printf("Node Name: %s\n", snapshot.node_name.c_str());
  Serial.printf("Node Type: %s\n", snapshot.node_type != nullptr ? snapshot.node_type : "");
  Serial.printf("Protocol:  %s\n", snapshot.protocol_version != nullptr ? snapshot.protocol_version : "");

  if (snapshot.wifi_connected) {
    Serial.printf("WiFi:      %s (%s)\n", snapshot.wifi_ssid.c_str(), snapshot.wifi_ip.c_str());
  } else {
    Serial.println("WiFi:      Disconnected");
  }

  if (snapshot.mqtt_connected) {
    Serial.printf("MQTT:      %s:%d\n", snapshot.mqtt_server.c_str(), snapshot.mqtt_port);
  } else {
    Serial.println("MQTT:      Disconnected");
  }

  Serial.printf("Signals:   %u total", (unsigned)snapshot.signal_count);
  if (snapshot.digital_signal_count > 0 || snapshot.analog_signal_count > 0) {
    Serial.printf(" (%u digital, %u analog)",
      (unsigned)snapshot.digital_signal_count,
      (unsigned)snapshot.analog_signal_count);
  }
  Serial.println();
  Serial.printf("SetupMode: %s\n", snapshot.setup_mode ? "YES" : "NO");

  if (handlers.print_custom_status != nullptr) {
    handlers.print_custom_status();
  }

  Serial.println("==========================\n");
}

inline void fabPrintDefaultHelp(const FabSerialStatusSnapshot& snapshot, const FabSerialHandlers& handlers) {
  Serial.printf("\n=== %s Serial Commands ===\n", snapshot.title);
  Serial.println("help or ?      - Show this help");
  Serial.println("status         - Show current status");
  Serial.println("topics         - List MQTT topics");
  Serial.println("values         - Show current signal values");
  Serial.println("reset          - Reset WiFi and MQTT settings");
  Serial.println("broker=IP      - Change MQTT broker");
  Serial.println("broker=IP:PORT - Change MQTT broker with port");
  Serial.println("mqtt=IP        - Same as broker=");

  if (handlers.print_custom_help != nullptr) {
    handlers.print_custom_help();
  }

  Serial.println("");
}

inline FabSerialCommandResult fabHandleSerialCommand(
  const String& rawCommand,
  int fallbackMqttPort,
  const FabSerialStatusSnapshot& snapshot,
  const FabSerialHandlers& handlers) {
  FabSerialCommandResult result;

  String command = rawCommand;
  command.trim();
  command.toLowerCase();
  if (command.length() == 0) return result;

  if (isResetCommand(command)) {
    result.handled = true;
    result.reset_requested = true;
    return result;
  }

  if (isBrokerCommand(command)) {
    result.handled = true;
    result.broker_command_received = true;
    result.broker_update_requested = parseBrokerCommand(command, result.broker_endpoint, fallbackMqttPort);
    return result;
  }

  if (command == "status") {
    fabPrintDefaultStatus(snapshot, handlers);
    result.handled = true;
    return result;
  }

  if (command == "topics") {
    if (handlers.print_topics != nullptr) {
      handlers.print_topics();
    } else {
      Serial.println("No topic printer registered.");
    }
    result.handled = true;
    return result;
  }

  if (command == "values") {
    if (handlers.print_values != nullptr) {
      handlers.print_values();
    } else {
      Serial.println("No value printer registered.");
    }
    result.handled = true;
    return result;
  }

  if (command == "help" || command == "?") {
    fabPrintDefaultHelp(snapshot, handlers);
    result.handled = true;
    return result;
  }

  if (handlers.handle_custom_command != nullptr && handlers.handle_custom_command(command)) {
    result.handled = true;
  }

  return result;
}

}  // namespace FabNodes
