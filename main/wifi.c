#include "wifi.h"

#include <string.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// ESP specific includes
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"

// WiFi
EventGroupHandle_t net_event_group;

void smartconfig_task(void *pvParameter) {
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    while (1) {
        uxBits = xEventGroupWaitBits(
            net_event_group, WIFI_CONNECTED_BIT | WIFI_ESPTOUCH_DONE_BIT, true,
            false, portMAX_DELAY);
        if (uxBits & WIFI_CONNECTED_BIT) {
            ESP_LOGI("WIFI", "WiFi Connected to ap");
        }
        if (uxBits & WIFI_ESPTOUCH_DONE_BIT) {
            ESP_LOGI("WIFI", "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

static void sc_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    switch (event_id) {
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI("WIFI", "SC_EVENT_FOUND_CHANNEL");
            break;
        case SC_EVENT_GOT_SSID_PSWD:
            ESP_LOGI("WIFI", "SC_EVENT_GOT_SSID_PSWD");
            smartconfig_event_got_ssid_pswd_t *evt =
                (smartconfig_event_got_ssid_pswd_t *)event_data;
            wifi_config_t wifi_config;
            uint8_t ssid[33] = {0};
            uint8_t password[65] = {0};
            bzero(&wifi_config, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, evt->ssid,
                   sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password,
                   sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set == true)
                memcpy(wifi_config.sta.bssid, evt->bssid,
                       sizeof(wifi_config.sta.bssid));

            memcpy(ssid, evt->ssid, sizeof(evt->ssid));
            memcpy(password, evt->password, sizeof(evt->password));

            ESP_LOGI("WIFI", "SSID:%s", evt->ssid);
            ESP_LOGI("WIFI", "PASSWORD:%s", evt->password);

            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case SC_EVENT_SEND_ACK_DONE:
            ESP_LOGI("WIFI", "SC_EVENT_SEND_ACK_DONE");
            if (event_data != NULL) {
                uint8_t phone_ip[4] = {0};
                memcpy(phone_ip, (uint8_t *)event_data, 4);
                ESP_LOGI("WIFI", "Phone ip: %d.%d.%d.%d\n", phone_ip[0],
                         phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(net_event_group, WIFI_ESPTOUCH_DONE_BIT);
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    switch (event_id) {
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(net_event_group, WIFI_CONNECTED_BIT);
            break;
    }
}

// Permanently try to connect to WiFi when connection is lost
// Never surrender!
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    switch (event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            // XXX Uncomment to reenable smartconfig
            // xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3,
            //            NULL);
            xEventGroupSetBits(net_event_group, WIFI_STARTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(net_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            break;
    }
}

void wifi_init(void) {
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    net_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID,
                                               &sc_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = CONFIG_DEFAULT_WIFI_ESSID,
                .password = CONFIG_DEFAULT_WIFI_PASSWD,
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
