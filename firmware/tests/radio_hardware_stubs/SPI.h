#pragma once
struct SPIClass {
    int begins = 0, sck = -1, miso = -1, mosi = -1, ss = -1;
    SPIClass() = default;
    explicit SPIClass(int) {}
    SPIClass(int, int, int, int) {}
    void begin(int ck, int mi, int mo, int cs = -1) {
        ++begins; sck = ck; miso = mi; mosi = mo; ss = cs;
    }
    void begin() { ++begins; }
};
extern SPIClass SPI;
#define NRF_SPIM2 2
