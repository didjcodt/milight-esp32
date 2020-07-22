#pragma once

#include "driver/i2c.h"

#define I2C_SLAVE_ADDR 0x53
#define I2C_SLAVE_RX_BUF_LEN 512
#define I2C_SLAVE_TX_BUF_LEN 512

void milight_init();

void send_key(int i2c_bus, uint8_t keycode);

// GPIO Definition
#define PIN_NUM_SDA1 12
#define PIN_NUM_SCL1 13
#define PIN_NUM_SDA2 19
#define PIN_NUM_SCL2 2
#define PIN_NUM_INT1 33
#define PIN_NUM_INT2 26
#define PIN_NUM_LED  25

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
