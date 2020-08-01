#include "milight.h"

#include <string.h>

// FreeRTOS includes
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_slave.h"
#include "soc/dport_reg.h"
#include "soc/i2c_reg.h"
#include "soc/i2c_struct.h"

// General command buffer is 5 bytes long, MSB is defined as the class.
//
// KEYS
// ====
//
// To press a key, the command is
// {0x02, 0x00, DATA, 0x00, 0x00}
// The third byte is defined as a set of flags:
// I2C 1                |  I2C 2
// ------------------------------------------
// MSB                  |  MSB
// 7   - SPEED PLUS     |  7   - ZONE 04 OFF
// 6   - MODE           |  6   - ZONE 04 ON
// 5   - SPEED MINUS    |  5   - ZONE 01 OFF
// 4   - GENERAL OFF    |  4   - ZONE 01 ON
// 3   - GENERAL ON     |  3   - ZONE 03 OFF
// 2   - Unused         |  2   - ZONE 03 ON
// 1   - Unused         |  1   - ZONE 02 OFF
// 0   - Unused         |  0   - ZONE 02 ON
// LSB                  |  LSB
// Set the byte to 0x00 to release the key
//
// SLIDERS
// =======
//
// Colour wheel
// ------------
// On I2C 1
// Command {0x03, DATA, 0x00, 0x00, 0x99}
// DATA is ranged on 0x00 - 0xFF
// 0x00 is blue
// 0x22
// 0x3C is red
// 0x5E
// 0x73 is yellow
// 0x9F
// 0xC3 is green
// 0xE1
//
// Temperature
// -----------
// On I2C 1
// Class 2 also?
// Command {0x06, 0x00, 0x00, 0x00, DATA}
// DATA is ranged on (left) 0xA0 --- 0x00 (middle) --- 0x99 (right)
//
// Saturation + Luminosity
// ----------
// On I2C 2
// The two sliders are two half sliders
// Command {0x03, DATA, 0x00, 0x00, 0x00}
// Saturation is ranged on 0x00 --- 0x7F
// Temperature is ranged on 0X80 --- 0xFF
//
static uint8_t no_touch[] = {0x02, 0x00, 0x00, 0x00, 0x00};
// static int slider_template[] = {0x03, 0x00, 0x00, 0x00, 0x00};

#define DATA_SIZE 5

static const char *TAG = "I2C";

/*
#define KEYSTATE_FORCER_STACK_SIZE 2048
StaticTask_t keystate_forcer_buffer;
StackType_t keystate_forcer_stack[KEYSTATE_FORCER_STACK_SIZE];
static void keystate_forcer(void *pvParameter) {
    while (1) {
        size_t d_size = i2c_slave_write_buffer(0, &keystate_0[0], DATA_SIZE,
portMAX_DELAY); for( int length=0; length<5; length++ ) { printf( "%d ",
keystate_0[ length ] );
        }
        printf( " total %d\n", d_size );
    }
}
*/

#define KEYPRESS_SIMULATOR_STACK_SIZE 2048
StaticTask_t keypress_simulator_buffer;
StackType_t keypress_simulator_stack[KEYPRESS_SIMULATOR_STACK_SIZE];
static void keypress_simulator(void *pvParameter) {
    uint8_t* keystate_0;

    while (1) {
        ESP_LOGI(TAG, "GENERAL ON");
        keystate_0[1] = GENERAL_ON;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "STOP");
        keystate_0[1] = 0x00;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "GENERAL OFF");
        keystate_0[1] = GENERAL_OFF;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "STOP");
        keystate_0[1] = 0x00;
        ESP_ERROR_CHECK(i2c_reset_tx_fifo(0));
    }

    while (1) {
        ESP_LOGI(TAG, "GENERAL ON");
        send_key(1, GENERAL_ON);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        send_key(1, RELEASE_KEY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "GENERAL OFF");
        send_key(1, GENERAL_OFF);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        send_key(1, RELEASE_KEY);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "ZONE 1 ON");
        send_key(1, ZONE_01_ON);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        send_key(1, RELEASE_KEY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "ZONE 1 OFF");
        send_key(1, ZONE_01_OFF);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        send_key(1, RELEASE_KEY);
    }
}

// TODO:
// - Set INT pin to 1
// - Send keycode
// - Set INT pin to 0
// - Wait for LED pin to be 0, or 50 ms
// - Set INT pin to 1
// - Send key release
// - Set INT pin to 0
// - Wait for LED pin to be 1
void send_key(int i2c_bus, uint8_t keycode) {
    // Take key payload template and add keycode in it
    uint8_t *data = (uint8_t *)malloc(DATA_SIZE);
    data = memcpy(data, no_touch, sizeof(no_touch));
    data[1] = keycode;

    uint8_t int_pin = i2c_bus == 0 ? PIN_NUM_INT1 : PIN_NUM_INT2;

    // Set INT pin to 1
    gpio_set_level(int_pin, 1);

    // Write keycode
    size_t d_size = i2c_slave_write_buffer(i2c_bus - 1, data, DATA_SIZE,
                                           1000 / portTICK_RATE_MS);
    if (d_size != DATA_SIZE) {
        ESP_LOGW(TAG,
                 "Key payload not written correctly, only %d bytes written out "
                 "of %d",
                 d_size, DATA_SIZE);
    }

    // Set INT pin to 0
    gpio_set_level(int_pin, 0);

    // Wait for 50ms
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Set INT pin to 1
    gpio_set_level(int_pin, 1);

    // Wait for 50ms
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Send key release
    d_size = i2c_slave_write_buffer(i2c_bus - 1, &no_touch[0], DATA_SIZE,
                                    1000 / portTICK_RATE_MS);
    if (d_size != DATA_SIZE) {
        ESP_LOGW(TAG,
                 "Key payload not written correctly, only %d bytes written out "
                 "of %d",
                 d_size, DATA_SIZE);
    }

    // Set INT pin to 0
    gpio_set_level(int_pin, 0);

    // Wait for 50ms
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Set INT pin to 1
    gpio_set_level(int_pin, 1);
}

void milight_init() {
    // Configure I2C slaves
    int i2c_slave_1 = I2C_NUM_0;
    int i2c_slave_2 = I2C_NUM_1;

    i2c_config_t conf_slave_1 = {
        .sda_io_num = PIN_NUM_SDA1,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_NUM_SCL1,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .mode = I2C_MODE_SLAVE,
        .slave = {.addr_10bit_en = 0, .slave_addr = I2C_MILIGHT_SLAVE_ADDR}};

    i2c_config_t conf_slave_2 = {
        .sda_io_num = PIN_NUM_SDA2,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_NUM_SCL2,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .mode = I2C_MODE_SLAVE,
        .slave = {.addr_10bit_en = 0, .slave_addr = I2C_MILIGHT_SLAVE_ADDR}};

    i2c_slave_param_config(i2c_slave_1, &conf_slave_1);
    i2c_slave_param_config(i2c_slave_2, &conf_slave_2);

    ESP_ERROR_CHECK(i2c_slave_driver_install(i2c_slave_1));
    ESP_ERROR_CHECK(i2c_slave_driver_install(i2c_slave_2));

    // Configure Interrupt pins and LED/ACK pin
    gpio_config_t conf_int = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << PIN_NUM_INT1) | (1ULL << PIN_NUM_INT2)),
        .pull_down_en = 0,
        .pull_up_en = 1};
    gpio_config(&conf_int);

    gpio_config_t conf_led = {.intr_type = GPIO_INTR_DISABLE,
                              .mode = GPIO_MODE_INPUT,
                              .pin_bit_mask = (1ULL << PIN_NUM_LED),
                              .pull_down_en = 0,
                              .pull_up_en = 0};
    gpio_config(&conf_led);

    // Create a High Priority task to force the i2c TX buffer to always be full
    // This is the only way as esp-idf is doing ultra weird things in the intr
    // :(
    // xTaskCreateStatic(&keystate_forcer, "keystate_forcer",
    //                  KEYSTATE_FORCER_STACK_SIZE, NULL, configMAX_PRIORITIES -
    //                  1, keystate_forcer_stack, &keystate_forcer_buffer);

    //xTaskCreateStatic(&keypress_simulator, "keypress_simulator",
    //                  KEYPRESS_SIMULATOR_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1,
    //                  keypress_simulator_stack, &keypress_simulator_buffer);
}
