#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <stdint.h>
#include <string>

namespace esphome {
namespace efergy_cc1101 {

class EfergyCc1101Component : public Component,
                             public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                                   spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_mains_voltage(float mains_voltage) { mains_voltage_ = mains_voltage; }
  void set_preferred_tx_id(const std::string &preferred_tx_id) { preferred_tx_id_spec_ = preferred_tx_id; }
  void set_publish_raw_bytes(bool publish_raw_bytes) { publish_raw_bytes_ = publish_raw_bytes; }
  void set_gdo0_pin(uint8_t pin_gdo0) { pin_gdo0_ = pin_gdo0; }
  void set_gdo2_pin(uint8_t pin_gdo2) { pin_gdo2_ = pin_gdo2; }

  void set_current_sensor(sensor::Sensor *sensor) { current_sensor_ = sensor; }
  void set_power_sensor(sensor::Sensor *sensor) { power_sensor_ = sensor; }
  void set_interval_sensor(sensor::Sensor *sensor) { interval_sensor_ = sensor; }
  void set_pairing_sensor(binary_sensor::BinarySensor *sensor) { pairing_sensor_ = sensor; }
  void set_tx_id_sensor(text_sensor::TextSensor *sensor) { tx_id_sensor_ = sensor; }
  void set_battery_state_sensor(text_sensor::TextSensor *sensor) { battery_state_sensor_ = sensor; }
  void set_raw_bytes_sensor(text_sensor::TextSensor *sensor) { raw_bytes_sensor_ = sensor; }

 protected:
  struct DecodeResult {
    uint16_t tx_id{0};
    bool battery_ok{false};
    bool pairing{false};
    int interval_s{0};
    float amps{0.0f};
    float watts{0.0f};
    char raw[32]{0};
    int quality{0};
  };

  static constexpr const char *const TAG = "efergy_cc1101";
  static constexpr int MAX_EDGES = 384;
  static constexpr uint16_t NOISE_MIN = 25;
  static constexpr uint16_t DATA_LONG_MAX = 260;
  static constexpr uint16_t SYNC_MIN = 300;
  static constexpr uint16_t SYNC_MAX = 1100;
  static constexpr uint32_t FINAL_GAP_US = 2500;
  static constexpr uint32_t MAX_CAPTURE_US = 20000;
  static constexpr uint32_t REARM_INTERVAL_MS = 60000;

  static EfergyCc1101Component *instance_;

  static void on_gdo0_change_isr_();
  static void on_gdo2_change_isr_();

  void begin_radio_();
  void rearm_if_needed_();
  void service_capture_();
  bool poll_packet_(DecodeResult &out);
  bool decode_packet_(const uint16_t *durations, const uint8_t *levels, int count, DecodeResult &out);
  bool decode_sync_windows_(const uint16_t *seq, int seq_n, int threshold, DecodeResult &out, int syncs, int data, int noise, int mode);
  bool decode_sliding_windows_(const uint16_t *seq, int seq_n, int threshold, DecodeResult &out, int syncs, int data, int noise, int mode);
  bool try_decode_bits_(const uint8_t *bits, int nbits, DecodeResult &out, int threshold, int mode, int syncs, int data, int noise);
  bool accept_candidate_(const DecodeResult &candidate);
  uint16_t active_target_tx_id_() const;
  void publish_decoded_(const DecodeResult &decoded);
  void log_preview_(const uint16_t *durations, const uint8_t *levels, int count);
  void on_gdo0_change_();
  void on_gdo2_change_();
  void finalize_capture_(uint32_t now);
  void strobe_(uint8_t cmd);
  void wr_(uint8_t addr, uint8_t val);
  uint8_t read_status_(uint8_t addr);
  int rssi_dbm_();
  uint16_t parse_id_(const char *spec) const;
  void apply_preferred_tx_id_();
  static void shift_left_(uint8_t *bytes, int nbytes);

  uint8_t pin_gdo0_{4};
  uint8_t pin_gdo2_{27};
  float mains_voltage_{230.0f};
  std::string preferred_tx_id_spec_{"auto"};
  bool publish_raw_bytes_{false};
  uint32_t last_rearm_ms_{0};
  uint32_t short_burst_count_{0};
  uint16_t last_tx_id_{0};
  uint32_t last_tx_seen_ms_{0};
  uint8_t last_tx_hits_{0};
  uint16_t locked_tx_id_{0};
  uint16_t preferred_tx_id_{0};

  volatile bool active_{false};
  volatile bool carrier_high_{false};
  volatile bool packet_ready_{false};
  volatile int edge_count_{0};
  volatile int ready_count_{0};
  volatile uint32_t capture_started_us_{0};
  volatile uint32_t last_edge_us_{0};
  volatile uint8_t last_level_{0};
  volatile uint16_t edge_durations_[MAX_EDGES]{};
  volatile uint8_t edge_levels_[MAX_EDGES]{};
  volatile uint16_t ready_durations_[MAX_EDGES]{};
  volatile uint8_t ready_levels_[MAX_EDGES]{};

  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *interval_sensor_{nullptr};
  binary_sensor::BinarySensor *pairing_sensor_{nullptr};
  text_sensor::TextSensor *tx_id_sensor_{nullptr};
  text_sensor::TextSensor *battery_state_sensor_{nullptr};
  text_sensor::TextSensor *raw_bytes_sensor_{nullptr};
};

}  // namespace efergy_cc1101
}  // namespace esphome
