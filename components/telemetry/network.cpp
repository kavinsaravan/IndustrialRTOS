#include "telemetry.hpp"
#include "sdkconfig.h"
#ifdef CONFIG_INDUSTRIAL_MQTT
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <atomic>
#include <cstring>

namespace industrial {
namespace {
std::atomic<bool> wifi_up{false},mqtt_up{false};
std::atomic<uint32_t> drops{0};
esp_mqtt_client_handle_t client=nullptr;
RetryBackoff wifi_retry,mqtt_retry;
bool initialized=false,started=false;
void wifi_event(void*,esp_event_base_t base,int32_t id,void*) {
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) wifi_up.store(true);
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        wifi_up.store(false); mqtt_up.store(false);
    }
}
void mqtt_event(void*,esp_event_base_t,int32_t id,void*) {
    if (id==MQTT_EVENT_CONNECTED) mqtt_up.store(true);
    if (id==MQTT_EVENT_DISCONNECTED || id==MQTT_EVENT_ERROR) mqtt_up.store(false);
}
}
bool network_initialize() {
    // Called only by the low-priority telemetry task. Offline control has already started.
    auto result=nvs_flash_init();
    if (result==ESP_ERR_NVS_NO_FREE_PAGES || result==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase()!=ESP_OK) return false;
        result=nvs_flash_init();
    }
    if (result!=ESP_OK || esp_netif_init()!=ESP_OK || esp_event_loop_create_default()!=ESP_OK) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init)!=ESP_OK) return false;
    if (esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr)!=ESP_OK ||
        esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,nullptr)!=ESP_OK) return false;
    wifi_config_t wifi{};
    std::strncpy(reinterpret_cast<char*>(wifi.sta.ssid),"Wokwi-GUEST",sizeof(wifi.sta.ssid)-1);
    if (esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK || esp_wifi_set_config(WIFI_IF_STA,&wifi)!=ESP_OK || esp_wifi_start()!=ESP_OK) return false;
    esp_mqtt_client_config_t config{};
    config.broker.address.uri=CONFIG_INDUSTRIAL_MQTT_URI;
    config.credentials.client_id=CONFIG_INDUSTRIAL_MQTT_CLIENT_ID;
    config.network.disable_auto_reconnect=true;
    config.network.timeout_ms=2000;
    config.outbox.limit=4096;
    config.task.priority=2;
    config.task.stack_size=6144;
    client=esp_mqtt_client_init(&config);
    if (!client || esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,mqtt_event,nullptr)!=ESP_OK) return false;
    initialized=true;
    return true;
}
void network_service(uint64_t now,const char* json) {
    if (!initialized) { ++drops; return; }
    if (!wifi_up.load()) {
        if (wifi_retry.attempt(now)) esp_wifi_connect();
        ++drops;
        return;
    }
    wifi_retry.connected();
    if (!mqtt_up.load()) {
        if (mqtt_retry.attempt(now)) {
            if (!started) started=esp_mqtt_client_start(client)==ESP_OK;
            else esp_mqtt_client_reconnect(client);
        }
        ++drops;
        return;
    }
    mqtt_retry.connected();
    // Bounded outbox; MQTT task handles socket sends. No network calls in control.
    if (esp_mqtt_client_enqueue(client,CONFIG_INDUSTRIAL_MQTT_TOPIC,json,0,0,0,true)<0) ++drops;
}
NetworkStatus network_status() { return {wifi_up.load(),mqtt_up.load(),drops.load()}; }
}
#else
namespace industrial {
bool network_initialize() { return true; }
void network_service(uint64_t,const char*) {}
NetworkStatus network_status() { return {}; }
}
#endif
