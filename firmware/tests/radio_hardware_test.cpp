#include <cassert>
#include "board_config.h"
#include "radio_hardware.h"

SPIClass SPI;

int main() {
    BoardConfig firstPins = BOARD;
    BoardConfig secondPins = BOARD;
    secondPins.pin_lora_nss = 11;
    secondPins.pin_lora_dio1 = 12;
    secondPins.pin_lora_rst = 13;
    secondPins.pin_lora_busy = 14;
    RadioHardware first(firstPins);
    RadioHardware second(secondPins);
    assert(&first.radio != &second.radio);
    assert(first.radio.module == &first.module);
    assert(second.radio.module == &second.module);
    assert(first.module.cs == firstPins.pin_lora_nss);
    assert(second.module.cs == secondPins.pin_lora_nss);
    assert(second.module.irq == secondPins.pin_lora_dio1);
    assert(second.module.rst == secondPins.pin_lora_rst);
    assert(second.module.busy == secondPins.pin_lora_busy);
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6) || defined(BOARD_RAK4631_WISMESH_ETH) || defined(BOARD_RAK4631_USB)
    assert(first.module.spi != &SPI);
    assert(first.module.spi != second.module.spi);
#else
    assert(first.module.spi == &SPI);
    assert(second.module.spi == &SPI); // shared legacy bus; P4 multi-bus is future work
#endif
    first.beginSpi();
    assert(first.module.spi->begins == ((firstPins.pin_lora_sck >= 0 || firstPins.pin_lora_miso >= 0 || firstPins.pin_lora_mosi >= 0) ? 1 : 0));
    assert(second.module.spi->begins == (first.module.spi == second.module.spi ? first.module.spi->begins : 0));
#if defined(ARDUINO_ARCH_ESP32)
    if (firstPins.pin_lora_sck >= 0 || firstPins.pin_lora_miso >= 0 || firstPins.pin_lora_mosi >= 0) {
        assert(first.module.spi->sck == firstPins.pin_lora_sck);
        assert(first.module.spi->miso == firstPins.pin_lora_miso);
        assert(first.module.spi->mosi == firstPins.pin_lora_mosi);
#if !defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
        assert(first.module.spi->ss == firstPins.pin_lora_nss);
#endif
    }
#endif
    assert(first.radio.startReceive() == 17);
    assert(first.radio.flags == (RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED)));
    assert(second.radio.flags == 0);
#if defined(BOARD_ETHERMESH_1W) || defined(BOARD_ESP32_P4_NANO)
    // Two independent P4 SPI peripherals with pin maps supplied per radio,
    // without changing the existing primary/global-SPI constructor.
    SPIClass spi2(0), spi3(1);
    RadioHardware onSpi2(BOARD, spi2, 21, 23, 22, 20);
    RadioHardware onSpi3(BOARD, spi3, 5, 6, 7, 8);
    assert(onSpi2.module.spi == &spi2);
    assert(onSpi3.module.spi == &spi3);
    assert(onSpi2.radio.module == &onSpi2.module);
    assert(onSpi3.radio.module == &onSpi3.module);
    assert(onSpi2.module.cs == 20);
    assert(onSpi3.module.cs == 8);
    assert(onSpi2.module.irq == BOARD.pin_lora_dio1);
    assert(onSpi3.module.rst == BOARD.pin_lora_rst);
    assert(onSpi3.module.busy == BOARD.pin_lora_busy);
    onSpi2.beginSpi();
    assert(spi2.begins == 1 && spi3.begins == 0);
    assert(spi2.sck == 21 && spi2.miso == 23 && spi2.mosi == 22 && spi2.ss == 20);
    onSpi3.beginSpi();
    assert(spi2.begins == 1 && spi3.begins == 1);
    assert(spi3.sck == 5 && spi3.miso == 6 && spi3.mosi == 7 && spi3.ss == 8);
    assert(first.module.spi == &SPI);
    assert(SPI.begins == 1); // injected begin did not reconfigure global SPI
#endif
}
