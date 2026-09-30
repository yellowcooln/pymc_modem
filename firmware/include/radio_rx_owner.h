#pragma once

#include <cstring>
#include "radio_command_context.h"

// Operations are bound to an explicit hardware/owner pair. prepareReceive is
// deliberately supplied by the caller: today's RFFrontEnd is primary-only and
// must not be driven by a secondary radio until it has independent RF policy.
template <typename Hardware, typename PrepareReceive>
bool startRadioReceive(Hardware& hardware, RadioRuntimeState& runtime,
                       PrepareReceive&& prepareReceive) {
    if (!runtime.receiveAllowed()) return true;
    prepareReceive();
    return hardware.radio.startReceive() == RADIOLIB_ERR_NONE;
}

template <typename Hardware, typename PrepareReceive, typename DisplayPacket,
          typename Broadcast, typename Clock>
void handleRadioRx(Hardware& hardware, RadioCommandContext& owner,
                   PrepareReceive&& prepareReceive, DisplayPacket&& displayPacket,
                   Broadcast&& broadcast, Clock&& clock) {
    auto& radio = hardware.radio;
    int len = radio.getPacketLength();
    if (len <= 0 || len > MAX_LORA_PAYLOAD) {
        startRadioReceive(hardware, owner.runtime, prepareReceive);
        return;
    }

    uint8_t rxBuf[MAX_LORA_PAYLOAD];
    int state = radio.readData(rxBuf, len);
    if (state != RADIOLIB_ERR_NONE) {
        owner.status.crc_errors++;
        startRadioReceive(hardware, owner.runtime, prepareReceive);
        return;
    }

    int16_t rssi = (int16_t)radio.getRSSI();
    int16_t snr = (int16_t)(radio.getSNR() * 10.0f);
    int16_t signalRssi = rssi;
    owner.status.rx_count++;
    owner.status.last_rssi = rssi;
    owner.status.last_snr = snr;
    displayPacket(owner.config.config(), rssi, snr);

    uint8_t rxPayload[6 + MAX_LORA_PAYLOAD];
    rxPayload[0] = rssi & 0xFF;
    rxPayload[1] = (rssi >> 8) & 0xFF;
    rxPayload[2] = snr & 0xFF;
    rxPayload[3] = (snr >> 8) & 0xFF;
    rxPayload[4] = signalRssi & 0xFF;
    rxPayload[5] = (signalRssi >> 8) & 0xFF;
    std::memcpy(rxPayload + 6, rxBuf, len);
    broadcast(CMD_RX_PACKET, rxPayload, 6 + len, owner.radioId);
    owner.runtime.noise.recordPacket(clock());
    startRadioReceive(hardware, owner.runtime, prepareReceive);
}
