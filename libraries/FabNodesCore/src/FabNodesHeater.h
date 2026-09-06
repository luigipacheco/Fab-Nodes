#pragma once

#include <Arduino.h>

// FabHeater — reusable closed-loop heater controller.
//
// PI control with time-proportional (slow window) switching, suitable for
// band/cartridge heaters driven by an SSR or MOSFET. Safety first:
//   - sensor fault -> heater off while faulted (never heat blind)
//   - overtemp     -> heater off and LATCHED until a new setTarget()
//   - target <= 0  -> heater off, integrator reset (no windup)
//   - forceOff()   -> immediate off; wire it into the node's safe state
//
// Dependency-free and node-agnostic: fab-struder uses it today; a future
// FabHeat node or any heated tool reuses it unchanged.
//
// Usage:
//   FabNodes::FabHeater heater;
//   heater.begin(cfg);                          // in setup()
//   heater.setTarget(tempTarget);               // when the target changes
//   heater.update(now, tempC, sensorOk);        // every sample tick
//   heater.forceOff();                          // in the safe-state callback

namespace FabNodes {

struct FabHeaterConfig {
  int pin = -1;                    // output pin; -1 disables the heater entirely
  bool active_high = true;         // SSR/MOSFET drive polarity
  float max_temp = 280.0f;         // hard cutoff (degC); latches until new target
  float kp = 8.0f;                 // % duty per degC of error
  float ki = 0.15f;                // % duty per degC-second (integral)
  float max_duty = 100.0f;         // duty clamp; derate an oversized heater here
  unsigned long window_ms = 1000;  // time-proportional switching window
  unsigned long pi_interval_ms = 250;  // control-loop update rate
};

class FabHeater {
 public:
  void begin(const FabHeaterConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.pin >= 0) {
      pinMode(cfg_.pin, OUTPUT);
      writeOutput(false);
    }
  }

  bool enabled() const { return cfg_.pin >= 0; }

  // Setting a positive target clears an overtemp latch (deliberate operator
  // action); target <= 0 switches off and resets the integrator.
  void setTarget(float target_c) {
    if (target_c <= 0) {
      target_ = 0;
      integral_ = 0;
      duty_ = 0;
      if (enabled()) writeOutput(false);
    } else {
      if (target_c != target_) overtemp_latched_ = false;
      target_ = target_c;
    }
  }

  void forceOff() {
    target_ = 0;
    integral_ = 0;
    duty_ = 0;
    if (enabled()) writeOutput(false);
  }

  // Call every loop/tick with the latest reading. sensor_ok = false when the
  // sensor is missing or faulted — output is forced off while that is true.
  void update(unsigned long now, float current_c, bool sensor_ok) {
    if (!enabled()) return;

    sensor_fault_ = !sensor_ok;
    if (sensor_ok && current_c >= cfg_.max_temp && target_ > 0) {
      overtemp_latched_ = true;
    }

    bool allow = sensor_ok && !overtemp_latched_ && target_ > 0;
    if (!allow) {
      duty_ = 0;
      integral_ = 0;
      last_pi_ms_ = now;
      writeOutput(false);
      return;
    }

    if (last_pi_ms_ == 0) last_pi_ms_ = now;
    if (now - last_pi_ms_ >= cfg_.pi_interval_ms) {
      float dt = (now - last_pi_ms_) / 1000.0f;
      last_pi_ms_ = now;

      float error = target_ - current_c;
      integral_ += error * dt;
      // Anti-windup: keep the integral term inside the duty range
      float i_term = cfg_.ki * integral_;
      if (i_term > cfg_.max_duty) integral_ = cfg_.max_duty / cfg_.ki;
      if (i_term < 0) integral_ = 0;

      duty_ = cfg_.kp * error + cfg_.ki * integral_;
      if (duty_ > cfg_.max_duty) duty_ = cfg_.max_duty;
      if (duty_ < 0) duty_ = 0;
    }

    // Time-proportional output over the switching window
    unsigned long phase = now % cfg_.window_ms;
    writeOutput(phase < (unsigned long)(duty_ / 100.0f * cfg_.window_ms));
  }

  float target() const { return target_; }
  float duty() const { return duty_; }           // 0..100 %
  bool outputOn() const { return output_on_; }
  bool sensorFault() const { return sensor_fault_; }
  bool overtempLatched() const { return overtemp_latched_; }
  bool faulted() const { return sensor_fault_ || overtemp_latched_; }

 private:
  void writeOutput(bool on) {
    output_on_ = on;
    digitalWrite(cfg_.pin, (on == cfg_.active_high) ? HIGH : LOW);
  }

  FabHeaterConfig cfg_;
  float target_ = 0;
  float duty_ = 0;
  float integral_ = 0;
  unsigned long last_pi_ms_ = 0;
  bool output_on_ = false;
  bool sensor_fault_ = false;
  bool overtemp_latched_ = false;
};

}  // namespace FabNodes
