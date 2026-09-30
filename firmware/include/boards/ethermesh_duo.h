// EtherMesh-Duo diagnostic target: two RAK13302 footprints, no RF operation.
// The fitted power/frequency variants, power path, and switch polarity have
// not been verified. Pins below document the schematic ONLY; none are driven.
#pragma once

inline const BoardConfig BOARD = {
    .name = "EtherMesh-Duo (diagnostic, NO RF)",
    .fw_suffix = "ethermesh_duo_no_rf",
    .mdns_prefix = "ethermesh-duo",
    // RF1 schematic: NSS20 SCK21 MOSI22 MISO23 RST26 ANT_SW27 BUSY32 DIO1 33.
    .pin_lora_nss = 20, .pin_lora_rst = 26, .pin_lora_busy = 32,
    .pin_lora_dio1 = 33, .pin_lora_sck = 21, .pin_lora_miso = 23,
    .pin_lora_mosi = 22,
    // RF2 schematic (not configured): DIO1=5 BUSY=6 RST=14 NSS=15
    // MISO=16 MOSI=17 SCK=18 ANT_SW=19.
    .rf_switch = { .en_pin = -1, .rx_pin = -1, .tx_pin = -1,
                   .dio2_as_rf_switch = false },
    .pin_i2c_sda = -1, .pin_i2c_scl = -1, .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,
    .pin_user_button = -1, .user_button_active_low = true,
    .battery = { .pin = -1 },
    .max_tx_power_dbm = 0,
    .use_dio3_tcxo = false, .tcxo_voltage = 0.0f,
    .has_lora_radio = false, .has_wifi = false,
    .has_network = true,
    .pin_protocol_uart_rx = -1, .pin_protocol_uart_tx = -1,
    .protocol_uart_baud = 921600,
    .ethernet = {
        .enabled = true, .phy_type = BoardConfig::EthernetPhy::IP101,
        .pin_mdc = 31, .pin_mdio = 52, .pin_phy_reset = 51,
        .phy_addr = 1, .rmii_clock_input = true,
        .use_static_ip = false,
        .static_ip = {192, 168, 5, 10}, .gateway = {192, 168, 5, 1},
        .subnet = {255, 255, 255, 0}, .dns = {192, 168, 5, 1},
    },
    .static_gpios = {}, .static_gpio_count = 0,
};
