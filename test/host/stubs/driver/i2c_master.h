#pragma once
#include "esp_err.h"

#include <cstddef>
#include <cstdint>
using i2c_master_bus_handle_t = void*;
using i2c_master_dev_handle_t = void*;
constexpr int I2C_ADDR_BIT_LEN_7 = 7;
struct i2c_device_config_t {
    int dev_addr_length;
    std::uint16_t device_address;
    std::uint32_t scl_speed_hz, scl_wait_us;
    struct {
        bool disable_ack_check;
    } flags;
};
int i2c_master_probe(void*, std::uint16_t, int);
int i2c_master_bus_add_device(void*, const i2c_device_config_t*, void**);
int i2c_master_bus_rm_device(void*);
int i2c_master_bus_reset(void*);
int i2c_master_transmit(void*, const std::uint8_t*, std::size_t, int);
int i2c_master_receive(void*, std::uint8_t*, std::size_t, int);
