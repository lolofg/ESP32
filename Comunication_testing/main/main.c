// main.c - connect to Wi-Fi, then send a message every 2 seconds with MQTT and/or CoAP
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "wifi.h"
#include "coap_send.h"
#include "secrets.h"

// ---- what to use (1 = on, 0 = off) ----
#define USE_MQTT          1
#define USE_COAP          1
#define COAP_CONFIRMABLE  true     // true = CON (server confirms, resent if lost), false = NON

static const char *TAG = "hello";

#if USE_MQTT
static volatile bool mqtt_connected = false;

// Called by the MQTT library whenever something happens (connected, sent, error, ...)
static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT: connected to the broker");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT: disconnected from the broker (reconnects by itself)");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:                       // only for QoS 1 and 2
        ESP_LOGI(TAG, "MQTT: broker confirmed message %d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT: error (is the broker running and reachable?)");
        break;
    default:
        break;
    }
}
#endif

void app_main(void)
{
    // 1) join the Wi-Fi network (waits until we have an IP address)
    wifi_connect();

#if USE_MQTT
    // 2a) start the MQTT client; it connects to the broker in the background
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    esp_mqtt_client_start(client);
#endif

#if USE_COAP
    // 2b) prepare CoAP (no connection needed: CoAP uses UDP)
    bool coap_ready = coap_client_start();
#endif

    // 3) send the same small message every 2 seconds
    int counter = 0;
    char message[64];
    while (true) {
        snprintf(message, sizeof(message), "{\"msg\":\"hello\",\"count\":%d}", counter++);

#if USE_MQTT
        if (mqtt_connected) {
            int id = esp_mqtt_client_publish(client, MQTT_TOPIC, message, 0, 1, 0);   // QoS 1
            ESP_LOGI(TAG, "MQTT: sent %s (message %d)", message, id);
        }
#endif
#if USE_COAP
        if (coap_ready) {
            ESP_LOGI(TAG, "CoAP: sending %s", message);
            coap_client_send(message, COAP_CONFIRMABLE);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}