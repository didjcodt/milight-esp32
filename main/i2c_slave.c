// This file is a fork from the i2c driver but with real slave support...
//
// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "driver/periph_ctrl.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_rom_gpio.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/xtensa_api.h"
#include "hal/i2c_hal.h"
#include "malloc.h"
#include "soc/dport_reg.h"
#include "soc/i2c_periph.h"
#include "soc/soc_memory_layout.h"

static const char *I2C_TAG = "i2c";

#define I2C_ENTER_CRITICAL_ISR(mux) portENTER_CRITICAL_ISR(mux)
#define I2C_EXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL_ISR(mux)
#define I2C_ENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#define I2C_EXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)

#define I2C_DRIVER_ERR_STR "i2c driver install error"
#define I2C_DRIVER_MALLOC_ERR_STR "i2c driver malloc error"
#define I2C_NUM_ERROR_STR "i2c number error"
#define I2C_ADDR_ERROR_STR "i2c null address error"
#define I2C_MODE_ERR_STR "i2c mode error"
#define I2C_SDA_IO_ERR_STR "sda gpio number error"
#define I2C_SCL_IO_ERR_STR "scl gpio number error"
#define I2C_SCL_SDA_EQUAL_ERR_STR "scl and sda gpio numbers are the same"
#define I2C_GPIO_PULLUP_ERR_STR "this i2c pin does not support internal pull-up"

#define I2C_FIFO_FULL_THRESH_VAL (28)
#define I2C_FIFO_EMPTY_THRESH_VAL (5)
#define I2C_IO_INIT_LEVEL (1)
#define I2C_SLAVE_TIMEOUT_DEFAULT \
    (32000) /* I2C slave timeout value, APB clock cycle number */
#define I2C_SLAVE_SDA_SAMPLE_DEFAULT \
    (10) /* I2C slave sample time after scl positive edge default value */
#define I2C_SLAVE_SDA_HOLD_DEFAULT \
    (10) /* I2C slave hold time after scl negative edge default value */

#define I2C_CONTEX_INIT_DEF(i2c_num)                                   \
    {                                                                  \
        .hal.dev = I2C_LL_GET_HW(i2c_num),                             \
        .spinlock = portMUX_INITIALIZER_UNLOCKED,                      \
        .hw_enabled = false,                                           \
    }

typedef struct {
    int i2c_num;                        /*!< I2C port number */
    intr_handle_t intr_handle;          /*!< I2C interrupt handle*/
    uint8_t data_buf[SOC_I2C_FIFO_LEN]; /*!< a buffer to store i2c data */
} i2c_obj_t;

typedef struct {
    i2c_hal_context_t hal; /*!< I2C hal context */
    portMUX_TYPE spinlock;
    bool hw_enabled;
#if !I2C_SUPPORT_HW_CLR_BUS
    int scl_io_num;
    int sda_io_num;
#endif
} i2c_context_t;

static i2c_context_t i2c_context[I2C_NUM_MAX] = {
    I2C_CONTEX_INIT_DEF(I2C_NUM_0),
    I2C_CONTEX_INIT_DEF(I2C_NUM_1),
};

static i2c_obj_t *p_i2c_obj[I2C_NUM_MAX] = {0};

static void i2c_slave_hw_enable(i2c_port_t i2c_num) {
    I2C_ENTER_CRITICAL(&(i2c_context[i2c_num].spinlock));
    if (i2c_context[i2c_num].hw_enabled != true) {
        periph_module_enable(i2c_periph_signal[i2c_num].module);
        i2c_context[i2c_num].hw_enabled = true;
    }
    I2C_EXIT_CRITICAL(&(i2c_context[i2c_num].spinlock));
}

#define DATA_SIZE 5
static uint8_t keystate_0[] = {0x02, 0x00, 0x00, 0x00, 0x00};
static uint8_t keystate_1[] = {0x02, 0x00, 0x00, 0x00, 0x00};

uint8_t* get_keystate(i2c_port_t i2c_num) {
    return i2c_num == I2C_NUM_0 ? &keystate_0[0] : &keystate_1[0];
}

static void IRAM_ATTR i2c_isr_handler(void *arg) {
    // Get back contextual data
    i2c_obj_t *p_i2c = (i2c_obj_t *)arg;
    int i2c_num = p_i2c->i2c_num;

    I2C_ENTER_CRITICAL_ISR(&(i2c_context[i2c_num].spinlock));
    i2c_intr_event_t evt_type = I2C_INTR_EVENT_ERR;
    i2c_hal_slave_handle_event(&(i2c_context[i2c_num].hal), &evt_type);

    // Use this in case you want to have named evt_type
    // const char* named[] = { "I2C_INTR_EVENT_ERR", "I2C_INTR_EVENT_ARBIT_LOST",
    //     "I2C_INTR_EVENT_NACK", "I2C_INTR_EVENT_TOUT", "I2C_INTR_EVENT_END_DET",
    //     "I2C_INTR_EVENT_TRANS_DONE", "I2C_INTR_EVENT_RXFIFO_FULL",
    //     "I2C_INTR_EVENT_TXFIFO_EMPTY" };

    // Acknowledge and temporarily disable i2c interrupts
    i2c_hal_disable_intr_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);

    // Available i2c interrupts are:
    // (in $(ESP_IDF)/components/soc/src/esp32/include/hal/i2c_ll.h)
    // - I2C_INTR_EVENT_ERR,
    // - I2C_INTR_EVENT_ARBIT_LOST,   /*!< I2C arbition lost event */
    // - I2C_INTR_EVENT_NACK,         /*!< I2C NACK event */
    // - I2C_INTR_EVENT_TOUT,         /*!< I2C time out event */
    // - I2C_INTR_EVENT_END_DET,      /*!< I2C end detected event */
    // - I2C_INTR_EVENT_TRANS_DONE,   /*!< I2C trans done event */
    // - I2C_INTR_EVENT_RXFIFO_FULL,  /*!< I2C rxfifo full event */
    // + I2C_INTR_EVENT_TXFIFO_EMPTY, /*!< I2C txfifo empty event */
    if (evt_type == I2C_INTR_EVENT_TXFIFO_EMPTY) {
        i2c_hal_write_txfifo(&(i2c_context[i2c_num].hal), &keystate_0[0], DATA_SIZE);
    }

    // Re-enable interrupts
    i2c_hal_clr_intsts_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);
    i2c_hal_enable_intr_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);
    I2C_EXIT_CRITICAL_ISR(&(i2c_context[i2c_num].spinlock));
}

static esp_err_t i2c_slave_set_pin(i2c_port_t i2c_num, int sda_io_num,
                                   int scl_io_num, bool sda_pullup_en,
                                   bool scl_pullup_en, i2c_mode_t mode) {
    int sda_in_sig, sda_out_sig, scl_in_sig, scl_out_sig;
    sda_out_sig = i2c_periph_signal[i2c_num].sda_out_sig;
    sda_in_sig = i2c_periph_signal[i2c_num].sda_in_sig;
    scl_out_sig = i2c_periph_signal[i2c_num].scl_out_sig;
    scl_in_sig = i2c_periph_signal[i2c_num].scl_in_sig;
    if (sda_io_num >= 0) {
        gpio_set_level(sda_io_num, I2C_IO_INIT_LEVEL);
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[sda_io_num], PIN_FUNC_GPIO);
        gpio_set_direction(sda_io_num, GPIO_MODE_INPUT_OUTPUT_OD);

        if (sda_pullup_en == GPIO_PULLUP_ENABLE) {
            gpio_set_pull_mode(sda_io_num, GPIO_PULLUP_ONLY);
        } else {
            gpio_set_pull_mode(sda_io_num, GPIO_FLOATING);
        }
        esp_rom_gpio_connect_out_signal(sda_io_num, sda_out_sig, 0, 0);
        esp_rom_gpio_connect_in_signal(sda_io_num, sda_in_sig, 0);
    }
    if (scl_io_num >= 0) {
        gpio_set_level(scl_io_num, I2C_IO_INIT_LEVEL);
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[scl_io_num], PIN_FUNC_GPIO);
        gpio_set_direction(scl_io_num, GPIO_MODE_INPUT_OUTPUT_OD);
        esp_rom_gpio_connect_out_signal(scl_io_num, scl_out_sig, 0, 0);
        esp_rom_gpio_connect_in_signal(scl_io_num, scl_in_sig, 0);
        if (scl_pullup_en == GPIO_PULLUP_ENABLE) {
            gpio_set_pull_mode(scl_io_num, GPIO_PULLUP_ONLY);
        } else {
            gpio_set_pull_mode(scl_io_num, GPIO_FLOATING);
        }
    }
#if !I2C_SUPPORT_HW_CLR_BUS
    i2c_context[i2c_num].scl_io_num = scl_io_num;
    i2c_context[i2c_num].sda_io_num = sda_io_num;
#endif
    return ESP_OK;
}

esp_err_t i2c_slave_driver_install(i2c_port_t i2c_num) {
    // Allocate i2c object
#if !CONFIG_SPIRAM_USE_MALLOC
    p_i2c_obj[i2c_num] = (i2c_obj_t *)calloc(1, sizeof(i2c_obj_t));
#else
    if (!(intr_alloc_flags & ESP_INTR_FLAG_IRAM)) {
        p_i2c_obj[i2c_num] = (i2c_obj_t *)calloc(1, sizeof(i2c_obj_t));
    } else {
        p_i2c_obj[i2c_num] = (i2c_obj_t *)heap_caps_calloc(
            1, sizeof(i2c_obj_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
#endif
    if (p_i2c_obj[i2c_num] == NULL) {
        ESP_LOGE(I2C_TAG, I2C_DRIVER_MALLOC_ERR_STR);
        return ESP_FAIL;
    }

    i2c_obj_t *p_i2c = p_i2c_obj[i2c_num];
    p_i2c->i2c_num = i2c_num;

#if CONFIG_SPIRAM_USE_MALLOC
    p_i2c->intr_alloc_flags = intr_alloc_flags;
#endif

    // Start i2c hardware
    i2c_slave_hw_enable(i2c_num);

    // Disable I2C interrupt
    i2c_hal_disable_intr_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);
    i2c_hal_clr_intsts_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);

    // Give first data to write
    for (int i=0; i<5; i++)
        i2c_hal_write_txfifo(&(i2c_context[i2c_num].hal), &keystate_0[0], DATA_SIZE);

    // Hook isr handler
    esp_intr_alloc(i2c_periph_signal[i2c_num].irq, 0, i2c_isr_handler,
                   p_i2c_obj[i2c_num], &p_i2c_obj[i2c_num]->intr_handle);

    // Enable I2C slave rx interrupt
    i2c_hal_enable_slave_rx_it(&(i2c_context[i2c_num].hal));

    ESP_LOGI(I2C_TAG, "I2C Port %d installed", i2c_num);
    return ESP_OK;
}

esp_err_t i2c_slave_param_config(i2c_port_t i2c_num,
                                 const i2c_config_t *i2c_conf) {
    esp_err_t ret = i2c_slave_set_pin(
        i2c_num, i2c_conf->sda_io_num, i2c_conf->scl_io_num,
        i2c_conf->sda_pullup_en, i2c_conf->scl_pullup_en, i2c_conf->mode);
    if (ret != ESP_OK) {
        return ret;
    }
    i2c_slave_hw_enable(i2c_num);

    I2C_ENTER_CRITICAL(&(i2c_context[i2c_num].spinlock));
    {
        // Clear all interrupts
        i2c_hal_disable_intr_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);
        i2c_hal_clr_intsts_mask(&(i2c_context[i2c_num].hal), I2C_INTR_MASK);

        // Initialize slave
        i2c_hal_context_t *hal = &(i2c_context[i2c_num].hal);
        i2c_hal_slave_init(hal, i2c_num);
        i2c_hal_set_fifo_mode(hal, true);
        i2c_hal_set_slave_addr(hal, i2c_conf->slave.slave_addr,
                               i2c_conf->slave.addr_10bit_en);
        i2c_hal_set_rxfifo_full_thr(hal, I2C_FIFO_FULL_THRESH_VAL);
        i2c_hal_set_txfifo_empty_thr(hal, I2C_FIFO_EMPTY_THRESH_VAL);

        // Set timing for data
        i2c_hal_set_sda_timing(hal, I2C_SLAVE_SDA_SAMPLE_DEFAULT,
                               I2C_SLAVE_SDA_HOLD_DEFAULT);
        i2c_hal_set_tout(hal, I2C_SLAVE_TIMEOUT_DEFAULT);

        // Enable interrupts
        i2c_hal_enable_slave_tx_it(hal);
    }
    I2C_EXIT_CRITICAL(&(i2c_context[i2c_num].spinlock));

    ESP_LOGI(I2C_TAG, "I2C Port %d configured", i2c_num);

    return ESP_OK;
}
