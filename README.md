# Efergy E2 Classic for ESPHome + CC1101

This package receives and decodes an Efergy E2 Classic energy monitor using an ESP32 and a CC1101 in FSK mode.

<img width="294" height="318"  alt="efergy ct clamp" src="https://github.com/user-attachments/assets/c5aafdd4-e60d-4944-a391-00c43ad7695b" />

## Why this layout

The decoder logic is still non-trivial because it has to:

- configure the CC1101 correctly for FSK reception
- capture carrier and data edges in real time
- reconstruct candidate packets from pulse timing windows
- validate checksum, battery, interval, and current fields
- reject false positives and auto-lock onto the real transmitter

So the goal is not to remove all of that code blindly. The cleaner approach is to hide it behind an ESPHome external component and a reusable package so users do not need helper headers, boot lambdas, or interval loops in their YAML.

The component now also follows the normal ESPHome SPI pattern internally: the package defines a standard top-level `spi:` bus, and the decoder registers as an SPI device on that bus instead of managing raw `SPI.begin()` transactions itself.

## What you get

- `components/efergy_cc1101` external component containing the decoder
- `packages/efergy_cc1101.yaml` reusable package with sensors and diagnostics
- energy sensor wired using ESPHome's built-in integration platform
- ESPHome-native SPI bus/device usage internally

## Files

- `packages/efergy_cc1101.yaml` - reusable package
- `components/efergy_cc1101/` - external component implementation
- `README.md` - setup guide and minimal user YAML example
- `.gitignore` - keeps local build artifacts and `secrets.yaml` out of git

## Supported hardware

- ESP32 board running ESPHome
- CC1101 433 MHz module
- Efergy CT Clamp Transmitter

## Default wiring

- CC1101 `SCK` -> ESP32 `GPIO14`
- CC1101 `MISO` -> ESP32 `GPIO12`
- CC1101 `MOSI` -> ESP32 `GPIO13`
- CC1101 `CSN` -> ESP32 `GPIO5`
- CC1101 `GDO0` -> ESP32 `GPIO4`
- CC1101 `GDO2` -> ESP32 `GPIO27`
- CC1101 `VCC` -> `3.3V`
- CC1101 `GND` -> `GND`

All of those pins can be changed through substitutions in your own device YAML.

## Standard ESPHome usage

The standard ESPHome consumer flow for a GitHub repo like this is:

- `external_components` pulls the custom component from the repo
- `packages` pulls the shared YAML from the repo
- the package defines the standard top-level `spi:` bus
- the component attaches to that bus as a normal ESPHome SPI device
- the user's device YAML only overrides substitutions

That keeps the repository standard for ESPHome and keeps the user's YAML short.

## Quick start

1. Create a local `secrets.yaml` next to your device YAML
2. Fill in your Wi-Fi details
3. Copy the minimal YAML from this README into your own device YAML
4. Change only the substitutions you need
5. Flash with ESPHome
6. Watch logs until the decoder locks onto your transmitter

## Minimal user YAML

```yaml
substitutions:
  device_name: efergy-sensor
  friendly_name: Efergy Energy Monitor
  board_type: esp32dev
  wifi_ssid: !secret wifi_ssid
  wifi_password: !secret wifi_password
  fallback_ap_password: !secret fallback_ap_password
  mains_voltage: "230.0"
  preferred_tx_id: "auto"

external_components:
  - source: github://91JJ/Efergy-CT-Clamp-Capture-for-ESP-Home@main
    components: [efergy_cc1101]
    refresh: 0s

spi:
  id: efergy_spi_bus
  clk_pin: ${pin_cc1101_sck}
  miso_pin: ${pin_cc1101_miso}
  mosi_pin: ${pin_cc1101_mosi}

efergy_cc1101:
  spi_id: efergy_spi_bus
  mains_voltage: ${mains_voltage}
  preferred_tx_id: "${preferred_tx_id}"
  publish_raw_bytes: ${publish_raw_bytes}
  cs_pin: ${pin_cc1101_csn}
  gdo0_pin: ${pin_cc1101_gdo0}
  gdo2_pin: ${pin_cc1101_gdo2}
  current:
    id: efergy_current_a
    name: "${friendly_name} Current"
  power:
    id: efergy_power_w
    name: "${friendly_name} Power Usage"
  interval:
    id: efergy_interval_s
    name: "${friendly_name} Interval"
  pairing:
    id: efergy_pairing
    name: "${friendly_name} Pairing Mode"
  tx_id:
    id: efergy_tx_id
    name: "${friendly_name} TX ID"
  battery_state:
    id: efergy_battery_state
    name: "${friendly_name} Battery State"
  raw_bytes:
    id: efergy_raw_bytes
    name: "${friendly_name} Raw Bytes"
    disabled_by_default: true

sensor:
  - platform: copy
    id: efergy_power_kw
    source_id: efergy_power_w
    internal: true
    unit_of_measurement: "kW"
    accuracy_decimals: 4
    filters:
      - multiply: 0.001

  - platform: integration
    id: efergy_energy_kwh
    name: "${friendly_name} Energy Usage"
    sensor: efergy_power_kw
    unit_of_measurement: "kWh"
    device_class: energy
    state_class: total_increasing
    time_unit: h
    restore: true
    accuracy_decimals: 3    
```

That is the intended end-user setup.

## Minimal user config

Most users only need to change:

- `device_name`
- `friendly_name`
- `wifi_ssid`
- `wifi_password`
- `fallback_ap_password`
- `mains_voltage`
- `preferred_tx_id`

Leave `preferred_tx_id: "auto"` for first setup. Once you know your transmitter ID, you can set it to that fixed value in either hex or decimal form.

## What got simpler

The user-facing YAML no longer needs:

- manual `includes`
- `on_boot` lambdas
- device-specific raw SPI transaction code
- `interval:` service loops
- template sensor publish lambdas

Those details now live inside the external component and package, while still using normal ESPHome building blocks like `spi`, `sensor`, `binary_sensor`, `text_sensor`, `copy`, and `integration`.

## Home Assistant entities

- `sensor.efergy_current`
- `sensor.efergy_power_usage`
- `sensor.efergy_energy_usage`
- `sensor.efergy_interval`
- `binary_sensor.efergy_pairing_mode`
- `sensor.efergy_tx_id`
- `sensor.efergy_battery_state`

Use the energy entity for the Home Assistant Energy dashboard, not the live power entity.

## Notes

- The decoder auto-locks to the best repeating transmitter by default
- `GDO2` must be connected for carrier detect in FSK mode
- Raw bytes are available as a disabled diagnostic entity if you set `publish_raw_bytes` to `true`
- The component currently targets one transmitter at a time, either auto-locked or fixed by ID

## Next improvements

Good candidates for future polish:

- optional multi-transmitter support
- optional accepted-packet logging toggle
- more formal release/test structure for publication
