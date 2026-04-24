#include "efergy_cc1101.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace efergy_cc1101 {

EfergyCc1101Component *EfergyCc1101Component::instance_ = nullptr;

float EfergyCc1101Component::get_setup_priority() const { return setup_priority::DATA; }

void EfergyCc1101Component::setup() {
  instance_ = this;
  apply_preferred_tx_id_();
  begin_radio_();
  last_rearm_ms_ = millis();
}

void EfergyCc1101Component::loop() {
  service_capture_();

  const uint32_t now = millis();
  if ((now - last_rearm_ms_) >= REARM_INTERVAL_MS) {
    rearm_if_needed_();
    last_rearm_ms_ = now;
  }

  DecodeResult decoded;
  if (poll_packet_(decoded)) {
    publish_decoded_(decoded);
  }
}

void EfergyCc1101Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Efergy CC1101:");
  ESP_LOGCONFIG(TAG, "  Mains voltage: %.1fV", mains_voltage_);
  ESP_LOGCONFIG(TAG, "  Preferred TX: %s", preferred_tx_id_spec_.c_str());
  ESP_LOGCONFIG(TAG, "  Raw bytes publish: %s", YESNO(publish_raw_bytes_));
  LOG_SPI_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  GDO pins: GDO0=%u GDO2=%u", pin_gdo0_, pin_gdo2_);
}

void EfergyCc1101Component::begin_radio_() {
  this->spi_setup();
  pinMode(pin_gdo0_, INPUT);
  pinMode(pin_gdo2_, INPUT);
  delay(10);

  detachInterrupt(digitalPinToInterrupt(pin_gdo0_));
  detachInterrupt(digitalPinToInterrupt(pin_gdo2_));
  active_ = false;
  packet_ready_ = false;
  edge_count_ = 0;
  ready_count_ = 0;

  strobe_(0x30);
  delay(10);
  strobe_(0x36);
  strobe_(0x3A);
  strobe_(0x3B);

  wr_(0x00, 0x0E);
  wr_(0x01, 0x2E);
  wr_(0x02, 0x0D);
  wr_(0x03, 0x47);
  wr_(0x06, 0xFF);
  wr_(0x07, 0x00);
  wr_(0x08, 0x32);
  wr_(0x0B, 0x06);
  wr_(0x0C, 0x00);
  wr_(0x0D, 0x10);
  wr_(0x0E, 0xAB);
  wr_(0x0F, 0xD1);
  wr_(0x10, 0x69);
  wr_(0x11, 0x5C);
  wr_(0x12, 0x00);
  wr_(0x13, 0x02);
  wr_(0x14, 0xF8);
  wr_(0x15, 0x45);
  wr_(0x17, 0x00);
  wr_(0x18, 0x18);
  wr_(0x19, 0x16);
  wr_(0x1A, 0x6C);
  wr_(0x1B, 0x43);
  wr_(0x1C, 0x40);
  wr_(0x1D, 0x91);
  wr_(0x20, 0xFB);
  wr_(0x21, 0xB6);
  wr_(0x23, 0xE9);
  wr_(0x24, 0x2A);
  wr_(0x25, 0x00);
  wr_(0x26, 0x1F);
  wr_(0x2C, 0x81);
  wr_(0x2D, 0x35);
  wr_(0x2E, 0x09);

  attachInterrupt(digitalPinToInterrupt(pin_gdo0_), on_gdo0_change_isr_, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pin_gdo2_), on_gdo2_change_isr_, CHANGE);

  strobe_(0x34);
  ESP_LOGI(TAG, "CC1101 ready: 433.55 MHz FSK RX (GDO0=%u, GDO2=%u)", pin_gdo0_, pin_gdo2_);
}

void EfergyCc1101Component::rearm_if_needed_() {
  uint8_t marc = read_status_(0x35) & 0x1F;
  if (marc != 0x0D) {
    ESP_LOGW(TAG, "CC1101 not in RX (MARC=0x%02X), re-arming", marc);
    detachInterrupt(digitalPinToInterrupt(pin_gdo0_));
    detachInterrupt(digitalPinToInterrupt(pin_gdo2_));
    active_ = false;
    packet_ready_ = false;
    strobe_(0x36);
    delayMicroseconds(500);
    strobe_(0x3A);
    delayMicroseconds(100);
    attachInterrupt(digitalPinToInterrupt(pin_gdo0_), on_gdo0_change_isr_, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pin_gdo2_), on_gdo2_change_isr_, CHANGE);
    strobe_(0x34);
  } else {
    ESP_LOGD(TAG, "CC1101 alive: RX RSSI=%d dBm", rssi_dbm_());
  }
}

void EfergyCc1101Component::service_capture_() {
  if (!active_)
    return;

  const uint32_t now = micros();
  const uint32_t since_last_edge = now - last_edge_us_;
  const bool timed_out = (!carrier_high_ && since_last_edge >= FINAL_GAP_US) || (now - capture_started_us_ >= MAX_CAPTURE_US);
  if (!timed_out)
    return;

  noInterrupts();
  finalize_capture_(now);
  interrupts();
}

bool EfergyCc1101Component::poll_packet_(DecodeResult &out) {
  uint16_t durations[MAX_EDGES];
  uint8_t levels[MAX_EDGES];
  int count = 0;

  noInterrupts();
  if (!packet_ready_) {
    interrupts();
    return false;
  }
  count = ready_count_;
  if (count > MAX_EDGES)
    count = MAX_EDGES;
  for (int i = 0; i < count; i++) {
    durations[i] = ready_durations_[i];
    levels[i] = ready_levels_[i];
  }
  packet_ready_ = false;
  ready_count_ = 0;
  interrupts();

  if (count < 16) {
    short_burst_count_++;
    if (count >= 4 || (short_burst_count_ % 20U) == 1U) {
      ESP_LOGD(TAG, "FSK burst too short (%d edges, dropped=%u)", count, (unsigned) short_burst_count_);
    }
    return false;
  }

  short_burst_count_ = 0;
  log_preview_(durations, levels, count);

  DecodeResult decoded;
  if (!decode_packet_(durations, levels, count, decoded)) {
    ESP_LOGD(TAG, "No decode (fsk edges=%d)", count);
    return false;
  }

  if (!accept_candidate_(decoded)) {
    ESP_LOGD(TAG, "Suppressed candidate tx=%04X q=%d raw=%s", decoded.tx_id, decoded.quality, decoded.raw);
    return false;
  }

  out = decoded;
  return true;
}

bool EfergyCc1101Component::decode_packet_(const uint16_t *durations, const uint8_t *levels, int count, DecodeResult &out) {
  static const int full_modes[] = {0, 1, 2};
  static const int focused_modes[] = {1, 0};
  static const int full_thresholds[] = {92, 100, 108, 116, 124, 132, 140, 148, 156};
  static const int focused_thresholds[] = {92, 100, 108};

  const uint16_t target_tx = active_target_tx_id_();
  const int *modes = (target_tx != 0) ? focused_modes : full_modes;
  const size_t mode_count = (target_tx != 0) ? 2 : 3;
  const int *thresholds = (target_tx != 0) ? focused_thresholds : full_thresholds;
  const size_t threshold_count = (target_tx != 0) ? 3 : 9;

  for (size_t mode_idx = 0; mode_idx < mode_count; mode_idx++) {
    int mode = modes[mode_idx];
    uint16_t seq[MAX_EDGES];
    int seq_n = 0;
    int syncs = 0;
    int data = 0;
    int noise = 0;

    for (int i = 0; i < count && seq_n < MAX_EDGES; i++) {
      if (mode == 1 && levels[i] == 0)
        continue;
      if (mode == 2 && levels[i] != 0)
        continue;
      uint16_t duration = durations[i];
      if (duration < NOISE_MIN) {
        noise++;
        continue;
      }
      seq[seq_n++] = duration;
      if (duration >= SYNC_MIN)
        syncs++;
      else
        data++;
    }

    if (seq_n < 64)
      continue;

    for (size_t threshold_idx = 0; threshold_idx < threshold_count; threshold_idx++) {
      int threshold = thresholds[threshold_idx];
      if (decode_sync_windows_(seq, seq_n, threshold, out, syncs, data, noise, mode))
        return true;
      if (target_tx == 0 && decode_sliding_windows_(seq, seq_n, threshold, out, syncs, data, noise, mode))
        return true;
    }

    if (target_tx != 0 && decode_sliding_windows_(seq, seq_n, 100, out, syncs, data, noise, mode)) {
      return true;
    }
  }

  return false;
}

bool EfergyCc1101Component::decode_sync_windows_(const uint16_t *seq, int seq_n, int threshold, DecodeResult &out, int syncs,
                                                 int data, int noise, int mode) {
  for (int i = 0; i < seq_n; i++) {
    if (seq[i] < SYNC_MIN || seq[i] > SYNC_MAX)
      continue;

    uint8_t bits[80]{};
    int nbits = 0;
    for (int j = i + 1; j < seq_n && nbits < 80; j++) {
      if (seq[j] >= SYNC_MIN)
        break;
      if (seq[j] > DATA_LONG_MAX)
        break;
      bits[nbits++] = (seq[j] >= threshold) ? 1 : 0;
    }

    if (try_decode_bits_(bits, nbits, out, threshold, mode, syncs, data, noise))
      return true;
  }

  return false;
}

bool EfergyCc1101Component::decode_sliding_windows_(const uint16_t *seq, int seq_n, int threshold, DecodeResult &out,
                                                    int syncs, int data, int noise, int mode) {
  uint8_t bits[80]{};
  for (int start = 0; start + 64 <= seq_n; start++) {
    int nbits = 0;
    for (int j = start; j < seq_n && nbits < 80; j++) {
      if (seq[j] >= SYNC_MIN)
        continue;
      if (seq[j] > DATA_LONG_MAX)
        break;
      bits[nbits++] = (seq[j] >= threshold) ? 1 : 0;
    }

    if (try_decode_bits_(bits, nbits, out, threshold, mode, syncs, data, noise))
      return true;
  }

  return false;
}

bool EfergyCc1101Component::try_decode_bits_(const uint8_t *bits, int nbits, DecodeResult &out, int threshold, int mode,
                                             int syncs, int data, int noise) {
  if (nbits < 64)
    return false;

  for (int start = 0; start <= nbits - 64; start++) {
    int candidate_bits = nbits - start;
    if (candidate_bits > 80)
      candidate_bits = 80;

    uint8_t bytes[16]{};
    int candidate_bytes = (candidate_bits + 7) / 8;

    for (int i = 0; i < candidate_bits; i++) {
      if (bits[start + i]) {
        bytes[i / 8] |= (uint8_t) (1U << (7 - (i % 8)));
      }
    }

    int num_bits = candidate_bits;
    while ((bytes[0] & 0xF0) != 0xF0 && (bytes[0] & 0xF0) != 0x00) {
      num_bits -= 1;
      if (num_bits < 64)
        break;
      shift_left_(bytes, candidate_bytes);
    }
    if (num_bits < 64)
      continue;

    if (bytes[0] & 0xF0) {
      for (int i = 0; i < 8; i++) {
        bytes[i] = (uint8_t) ~bytes[i];
      }
    }

    int zero_count = 0;
    for (int i = 0; i < 8; i++) {
      if (bytes[i] == 0)
        zero_count++;
    }
    if (zero_count > 5)
      continue;

    unsigned checksum = 0;
    for (int i = 0; i < 7; i++) {
      checksum += bytes[i];
    }
    if ((checksum & 0xFFU) == 0)
      continue;
    if ((checksum & 0xFFU) != bytes[7])
      continue;

    uint16_t tx_id = (uint16_t) ((bytes[2] << 8) | bytes[1]);
    const uint16_t target_tx = active_target_tx_id_();
    if (target_tx != 0 && tx_id != target_tx)
      continue;

    uint8_t flags = bytes[3];
    bool pairing = (flags & 0x80) != 0;
    bool battery_ok = (flags & 0x40) != 0;
    int interval_s = 0;
    switch (flags & 0x30) {
      case 0x00:
        interval_s = 10;
        break;
      case 0x10:
        interval_s = 15;
        break;
      case 0x20:
        interval_s = 20;
        break;
      default:
        interval_s = (((flags & 0x30) >> 4) + 1) * 6;
        break;
    }

    int fact = 15 - (int) (int8_t) bytes[6];
    if (fact < 7 || fact > 23)
      continue;

    float amps = (float) ((bytes[4] << 8) | bytes[5]) / (float) (1UL << fact);
    if (!(amps >= 0.0f && amps <= 200.0f))
      continue;

    int quality = 0;
    if (bytes[0] == 0x0D)
      quality += 6;
    if ((bytes[3] & 0x0F) == 0x00)
      quality += 2;
    if ((bytes[3] & 0x80) == 0)
      quality += 2;
    if ((bytes[3] & 0x40) != 0)
      quality += 2;
    if (bytes[1] != 0x00 || bytes[2] != 0x80)
      quality += 3;
    if (amps >= 0.1f && amps <= 120.0f)
      quality += 4;
    if (data >= 120)
      quality += 2;
    if (noise <= 80)
      quality += 1;
    if (tx_id == 0x0080)
      quality -= 8;
    if (bytes[0] == 0x00 && bytes[3] == 0x80)
      quality -= 8;
    if (bytes[4] == 0x00 && bytes[5] == 0x80 && bytes[6] == 0x00)
      quality -= 6;
    if (bytes[4] == 0x00 && bytes[5] == 0x04 && bytes[6] == 0x00)
      quality -= 4;

    out.tx_id = tx_id;
    out.battery_ok = battery_ok;
    out.pairing = pairing;
    out.interval_s = interval_s;
    out.amps = amps;
    out.watts = amps * mains_voltage_;
    out.quality = quality;
    snprintf(out.raw, sizeof(out.raw), "%02X %02X %02X %02X %02X %02X %02X %02X", bytes[0], bytes[1], bytes[2],
             bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
    return true;
  }

  return false;
}

bool EfergyCc1101Component::accept_candidate_(const DecodeResult &candidate) {
  const uint32_t now = millis();

  if (preferred_tx_id_ != 0) {
    return candidate.tx_id == preferred_tx_id_ && candidate.quality >= 4;
  }

  if (locked_tx_id_ != 0) {
    return candidate.tx_id == locked_tx_id_ && candidate.quality >= 4;
  }

  if (candidate.quality < 4)
    return false;

  for (auto &contender : lock_contenders_) {
    if (contender.tx_id != 0 && (now - contender.last_seen_ms) > LOCK_CANDIDATE_TIMEOUT_MS) {
      contender = LockContender{};
    }
  }

  if (candidate.tx_id == last_tx_id_ && (now - last_tx_seen_ms_) <= 30000U) {
    if (last_tx_hits_ < 255)
      last_tx_hits_++;
  } else {
    last_tx_id_ = candidate.tx_id;
    last_tx_hits_ = 1;
  }
  last_tx_seen_ms_ = now;

  const bool plausible_lock_candidate = candidate.quality >= LOCK_MIN_QUALITY && !candidate.pairing && candidate.tx_id != 0 &&
                                       candidate.tx_id != 0x0080 && candidate.interval_s >= 8 && candidate.interval_s <= 24;

  if (plausible_lock_candidate) {
    LockContender *contender = nullptr;
    LockContender *empty_slot = nullptr;
    LockContender *weakest_slot = &lock_contenders_[0];

    for (auto &slot : lock_contenders_) {
      if (slot.tx_id == candidate.tx_id) {
        contender = &slot;
        break;
      }
      if (slot.tx_id == 0 && empty_slot == nullptr) {
        empty_slot = &slot;
      }
      if (slot.hits < weakest_slot->hits ||
          (slot.hits == weakest_slot->hits && slot.best_quality < weakest_slot->best_quality) ||
          (slot.hits == weakest_slot->hits && slot.best_quality == weakest_slot->best_quality && slot.last_seen_ms < weakest_slot->last_seen_ms)) {
        weakest_slot = &slot;
      }
    }

    if (contender == nullptr) {
      contender = (empty_slot != nullptr) ? empty_slot : weakest_slot;
      *contender = LockContender{};
      contender->tx_id = candidate.tx_id;
      contender->hits = 1;
      contender->best_quality = candidate.quality;
      contender->interval_s = (uint8_t) candidate.interval_s;
      contender->last_seen_ms = now;
    } else {
      const uint32_t age_ms = now - contender->last_seen_ms;
      const uint32_t expected_ms = (uint32_t) std::max<int>(candidate.interval_s, contender->interval_s) * 1000U;
      const bool interval_matches = age_ms >= 3000U &&
                                    (age_ms + LOCK_INTERVAL_TOLERANCE_MS >= expected_ms) &&
                                    (age_ms <= expected_ms * 2U + LOCK_INTERVAL_TOLERANCE_MS);

      if (interval_matches) {
        if (contender->hits < 255)
          contender->hits++;
      } else {
        contender->hits = 1;
      }

      contender->best_quality = std::max(contender->best_quality, candidate.quality);
      contender->interval_s = (uint8_t) candidate.interval_s;
      contender->last_seen_ms = now;
    }

    if (contender->hits >= LOCK_MIN_HITS) {
      locked_tx_id_ = contender->tx_id;
      ESP_LOGI(TAG, "Locked TX candidate %04X after %u matching frames", locked_tx_id_, contender->hits);
      return true;
    }
  }

  return candidate.quality >= 12;
}

uint16_t EfergyCc1101Component::active_target_tx_id_() const {
  if (preferred_tx_id_ != 0)
    return preferred_tx_id_;
  return locked_tx_id_;
}

void EfergyCc1101Component::publish_decoded_(const DecodeResult &decoded) {
  ESP_LOGI(TAG, "Packet tx=%04X %.3fA %.1fW batt=%s int=%ds", decoded.tx_id, decoded.amps, decoded.watts,
           decoded.battery_ok ? "OK" : "LOW", decoded.interval_s);

  if (current_sensor_ != nullptr)
    current_sensor_->publish_state(decoded.amps);
  if (power_sensor_ != nullptr)
    power_sensor_->publish_state(decoded.watts);
  if (interval_sensor_ != nullptr)
    interval_sensor_->publish_state((float) decoded.interval_s);
  if (pairing_sensor_ != nullptr)
    pairing_sensor_->publish_state(decoded.pairing);
  if (tx_id_sensor_ != nullptr) {
    char txbuf[6];
    snprintf(txbuf, sizeof(txbuf), "%u", (unsigned) decoded.tx_id);
    tx_id_sensor_->publish_state(txbuf);
  }
  if (battery_state_sensor_ != nullptr)
    battery_state_sensor_->publish_state(decoded.battery_ok ? "OK" : "LOW");
  if (publish_raw_bytes_ && raw_bytes_sensor_ != nullptr)
    raw_bytes_sensor_->publish_state(decoded.raw);
}

void EfergyCc1101Component::log_preview_(const uint16_t *durations, const uint8_t *levels, int count) {
  int syncs = 0;
  int data = 0;
  int noise = 0;
  for (int i = 0; i < count; i++) {
    if (durations[i] < NOISE_MIN)
      noise++;
    else if (durations[i] >= SYNC_MIN)
      syncs++;
    else
      data++;
  }

  ESP_LOGD(TAG, "fsk edges=%d sync>=%d:%d data:%d noise<%d:%d", count, SYNC_MIN, syncs, data, NOISE_MIN, noise);
  char buf[280];
  int pos = snprintf(buf, sizeof(buf), "RLE(%d):", count);
  for (int i = 0; i < count && i < 40 && pos < (int) sizeof(buf) - 8; i++) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, " %c%u", levels[i] ? 'H' : 'L', durations[i]);
  }
  ESP_LOGD(TAG, "%s", buf);
}

void EfergyCc1101Component::on_gdo0_change_isr_() {
  if (instance_ != nullptr)
    instance_->on_gdo0_change_();
}

void EfergyCc1101Component::on_gdo2_change_isr_() {
  if (instance_ != nullptr)
    instance_->on_gdo2_change_();
}

void EfergyCc1101Component::on_gdo2_change_() {
  bool high = digitalRead(pin_gdo2_);
  uint32_t now = micros();

  if (high) {
    carrier_high_ = true;
    active_ = true;
    packet_ready_ = false;
    edge_count_ = 0;
    capture_started_us_ = now;
    last_edge_us_ = now;
    last_level_ = digitalRead(pin_gdo0_);
    return;
  }

  if (!active_)
    return;
  carrier_high_ = false;
}

void EfergyCc1101Component::on_gdo0_change_() {
  if (!active_)
    return;

  uint32_t now = micros();
  uint32_t duration = now - last_edge_us_;
  if (edge_count_ < MAX_EDGES) {
    edge_durations_[edge_count_] = (duration > 65535UL) ? 65535 : (uint16_t) duration;
    edge_levels_[edge_count_] = last_level_;
    edge_count_++;
  }
  last_edge_us_ = now;
  last_level_ = digitalRead(pin_gdo0_);

  if (edge_count_ >= MAX_EDGES) {
    finalize_capture_(now);
  }
}

void EfergyCc1101Component::finalize_capture_(uint32_t now) {
  if (!active_)
    return;

  uint32_t duration = now - last_edge_us_;
  if (edge_count_ < MAX_EDGES) {
    edge_durations_[edge_count_] = (duration > 65535UL) ? 65535 : (uint16_t) duration;
    edge_levels_[edge_count_] = last_level_;
    edge_count_++;
  }

  int count = edge_count_;
  if (count > MAX_EDGES)
    count = MAX_EDGES;

  for (int i = 0; i < count; i++) {
    ready_durations_[i] = edge_durations_[i];
    ready_levels_[i] = edge_levels_[i];
  }

  ready_count_ = count;
  packet_ready_ = count > 0;
  active_ = false;
  carrier_high_ = false;
}

void EfergyCc1101Component::strobe_(uint8_t cmd) {
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void EfergyCc1101Component::wr_(uint8_t addr, uint8_t val) {
  this->enable();
  this->write_byte(addr & 0x3F);
  this->write_byte(val);
  this->disable();
}

uint8_t EfergyCc1101Component::read_status_(uint8_t addr) {
  this->enable();
  this->write_byte(0xC0 | (addr & 0x3F));
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

int EfergyCc1101Component::rssi_dbm_() {
  uint8_t raw = read_status_(0x34);
  return (raw >= 128) ? ((int) (int8_t) raw / 2) - 74 : (raw / 2) - 74;
}

uint16_t EfergyCc1101Component::parse_id_(const char *spec) const {
  if (spec == nullptr || *spec == '\0')
    return 0;

  while (*spec == ' ' || *spec == '\t')
    spec++;
  if (*spec == '\0' || strcasecmp(spec, "auto") == 0)
    return 0;

  bool has_hex_alpha = false;
  bool all_digits = true;
  for (const char *p = spec; *p != '\0'; ++p) {
    if ((*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f'))
      has_hex_alpha = true;
    if (*p < '0' || *p > '9')
      all_digits = false;
  }

  char *end = nullptr;
  unsigned long value = 0;
  if ((spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X')) || has_hex_alpha) {
    value = strtoul(spec, &end, 16);
  } else if (all_digits) {
    value = strtoul(spec, &end, 10);
  } else {
    value = strtoul(spec, &end, 0);
  }

  if (end == spec || *end != '\0' || value == 0 || value > 0xFFFFUL)
    return 0;
  return (uint16_t) value;
}

void EfergyCc1101Component::apply_preferred_tx_id_() {
  preferred_tx_id_ = parse_id_(preferred_tx_id_spec_.c_str());
  locked_tx_id_ = preferred_tx_id_;
  last_tx_id_ = preferred_tx_id_;
  last_tx_hits_ = 0;
  for (auto &contender : lock_contenders_) {
    contender = LockContender{};
  }
  if (preferred_tx_id_ == 0) {
    ESP_LOGI(TAG, "TX selection: auto");
  } else {
    ESP_LOGI(TAG, "TX selection fixed to %04X", preferred_tx_id_);
  }
}

void EfergyCc1101Component::shift_left_(uint8_t *bytes, int nbytes) {
  for (int i = 0; i < nbytes; i++) {
    uint8_t next = (i + 1 < nbytes) ? bytes[i + 1] : 0;
    bytes[i] = (uint8_t) ((bytes[i] << 1) | ((next & 0x80) ? 1 : 0));
  }
}

}  // namespace efergy_cc1101
}  // namespace esphome
