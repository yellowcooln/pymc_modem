#pragma once

#include <SPI.h>
#include <RadioLib.h>
#include "board_config.h"

// Preserve the preamble IRQ used by the passive reception guard.
class OpenHopSX1262 : public SX1262 {
public:
    using SX1262::SX1262;

    int16_t startReceive() override {
        return SX1262::startReceive(
            RADIOLIB_SX126X_RX_TIMEOUT_INF,
            RADIOLIB_IRQ_RX_DEFAULT_FLAGS |
                (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
            RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    int16_t applyRegisterPatch08B5() {
        uint8_t value = 0;
        int16_t state = readRegister(0x08B5, &value, 1);
        if (state != RADIOLIB_ERR_NONE) return state;
        value |= 0x01;
        return writeRegister(0x08B5, &value, 1);
    }
};

// Construction/lifetime of the radio and its Module live together. The
// dedicated buses on Photon and RAK remain per-instance; other boards borrow
// global SPI exactly as before (including the current single-radio P4 path).
// The explicit-bus constructor borrows an externally owned SPIClass, which
// must outlive this object; it does not affect the legacy primary radio.
class RadioHardware {
public:
    explicit RadioHardware(const BoardConfig& pins)
        : pins_(pins),
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6) || defined(BOARD_RAK4631_WISMESH_ETH) || defined(BOARD_RAK4631_USB)
          module(pins.pin_lora_nss, pins.pin_lora_dio1,
                 pins.pin_lora_rst, pins.pin_lora_busy, loraSpi_),
#else
          module(pins.pin_lora_nss, pins.pin_lora_dio1,
                 pins.pin_lora_rst, pins.pin_lora_busy),
#endif
          radio(&module) {}

#ifdef ARDUINO_ARCH_ESP32
    RadioHardware(const BoardConfig& pins, SPIClass& bus,
                  int sck, int miso, int mosi, int nss)
        : pins_(pins), injectedSpi_(&bus),
          sck_(sck), miso_(miso), mosi_(mosi), nss_(nss),
          module(nss, pins.pin_lora_dio1, pins.pin_lora_rst,
                 pins.pin_lora_busy, bus),
          radio(&module) {}
#endif

    RadioHardware(const RadioHardware&) = delete;
    RadioHardware& operator=(const RadioHardware&) = delete;

    void beginSpi() {
#ifdef ARDUINO_ARCH_ESP32
        if (injectedSpi_) {
            injectedSpi_->begin(sck_, miso_, mosi_, nss_);
            return;
        }
#endif
        if (pins_.pin_lora_sck < 0 && pins_.pin_lora_miso < 0 && pins_.pin_lora_mosi < 0)
            return;
#ifdef ARDUINO_ARCH_ESP32
#  if defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
        loraSpi_.begin(pins_.pin_lora_sck, pins_.pin_lora_miso, pins_.pin_lora_mosi);
#  else
        SPI.begin(pins_.pin_lora_sck, pins_.pin_lora_miso,
                  pins_.pin_lora_mosi, pins_.pin_lora_nss);
#  endif
#else
#  if defined(BOARD_RAK4631_WISMESH_ETH) || defined(BOARD_RAK4631_USB)
        loraSpi_.begin();
#  else
        SPI.begin();
#  endif
#endif
    }

private:
    const BoardConfig& pins_;
#ifdef ARDUINO_ARCH_ESP32
    SPIClass* injectedSpi_ = nullptr;
    int sck_ = -1, miso_ = -1, mosi_ = -1, nss_ = -1;
#endif
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
    SPIClass loraSpi_{0};
#elif defined(BOARD_RAK4631_WISMESH_ETH) || defined(BOARD_RAK4631_USB)
    // RAK4631 internal radio is on SPIM2 P1.11/P1.13/P1.12;
    // global SPI remains available to the W5100S Ethernet controller.
    SPIClass loraSpi_{NRF_SPIM2, 45, 43, 44};
#endif
public:
    Module module; // must outlive radio
    OpenHopSX1262 radio;
};
