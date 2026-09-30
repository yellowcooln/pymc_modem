#pragma once
#include "protocol.h"

// Called after endpoint ownership and query dispatch, before any mutating
// command handler. On a diagnostic board no radio handler may run.
template <typename Reject>
bool rejectUnavailableRadioCommand(uint8_t cmd, bool hasRadio, bool ready,
                                   Reject reject) {
    if (hasRadio && ready) return false;
    switch (cmd) {
    case CMD_TX_REQUEST:
    case CMD_SET_CONFIG:
    case CMD_CAD_REQUEST:
    case CMD_RX_START:
    case CMD_SET_CAD_PARAMS:
    case CMD_SET_AUTO_CAD:
    case CMD_RADIO_STANDBY:
    case CMD_RADIO_RESUME:
        reject(ERR_NO_RADIO);
        return true;
    default:
        return false;
    }
}
