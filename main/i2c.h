#pragma once

#include "driver/i2c.h"

#define I2C_SLAVE_ADDR 0x53
#define I2C_SLAVE_RX_BUF_LEN 512
#define I2C_SLAVE_TX_BUF_LEN 512

void i2c_init(int sda1, int scl1, int sda2, int scl2);

void send_key(int i2c_bus, uint8_t keycode);

#define RELEASE_KEY 0x00
// Keycode defines for I2C 1
#define GENERAL_ON (0x01 << 3)
#define GENERAL_OFF (0x01 << 4)
#define SPEED_MINUS (0x01 << 5)
#define MODE (0x01 << 6)
#define SPEED_PLUS (0x01 << 7)

// Keycode defines for I2C 2
#define ZONE_01_OFF (0x01 << 5)
#define ZONE_01_ON (0x01 << 4)
#define ZONE_02_OFF (0x01 << 1)
#define ZONE_02_ON (0x01 << 0)
#define ZONE_03_OFF (0x01 << 3)
#define ZONE_03_ON (0x01 << 2)
#define ZONE_04_OFF (0x01 << 7)
#define ZONE_04_ON (0x01 << 6)
