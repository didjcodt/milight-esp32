#pragma once

#include <esp_types.h>

esp_err_t i2c_slave_driver_install(i2c_port_t);
esp_err_t i2c_slave_param_config(i2c_port_t, i2c_config_t*);
