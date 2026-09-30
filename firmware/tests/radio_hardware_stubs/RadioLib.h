#pragma once
#include <cstdint>
#include "SPI.h"
#define RADIOLIB_SX126X_RX_TIMEOUT_INF 0
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 2UL
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 3UL
#define RADIOLIB_IRQ_PREAMBLE_DETECTED 4
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_NC 0xFFFFFFFFUL
struct Module {
    int cs, irq, rst, busy;
    SPIClass* spi;
    Module(int c, int i, int r, int b) : Module(c, i, r, b, SPI) {}
    Module(int c, int i, int r, int b, SPIClass& s)
        : cs(c), irq(i), rst(r), busy(b), spi(&s) {}
};
struct SX1262 {
    Module* module;
    unsigned long flags = 0;
    explicit SX1262(Module* m) : module(m) {}
    virtual ~SX1262() = default;
    virtual int16_t startReceive() { return 0; }
    int16_t startReceive(unsigned long, unsigned long f, unsigned long, int) {
        flags = f; return 17;
    }
    int16_t readRegister(unsigned, uint8_t* v, unsigned) { *v = 0; return 0; }
    int16_t writeRegister(unsigned, uint8_t*, unsigned) { return 0; }
};
