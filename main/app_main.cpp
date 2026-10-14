#include "control.hpp"
#include "diagnostics.hpp"
#include "telemetry.hpp"
#include "sensors.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>

namespace {
using namespace industrial;
constexpr gpio_num_t MOTOR = GPIO_NUM_18, FAN = GPIO_NUM_19, ALARM = GPIO_NUM_23;
constexpr gpio_num_t ACK = GPIO_NUM_25;
constexpr uint32_t ACK_EDGE = 1;
constexpr size_t SENSOR_QUEUE_LENGTH = 8, EVENT_QUEUE_LENGTH = 16;
constexpr uint64_t CONTROL_PERIOD_MS = 50;
constexpr uint32_t SAFETY_TASK_MASK = (1u << 0) | (1u << 1) | (1u << 2);
const char* TAG = "IndustrialRTOS";
uint64_t epoch_ms = 0;
std::atomic<bool> startup_ready{false};
void wait_for_start() { while (!startup_ready.load()) vTaskDelay(pdMS_TO_TICKS(1)); }
uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()/1000) - epoch_ms; }
Sensors sensors;
Controller controller;
Diagnostics diagnostics;
TaskHandle_t handles[TASK_COUNT]{};
QueueHandle_t readings, events;
SemaphoreHandle_t snapshot_mutex;
StaticSemaphore_t mutex_storage;
StaticQueue_t sensor_queue_storage, event_queue_storage;
alignas(Sample) uint8_t sensor_buffer[SENSOR_QUEUE_LENGTH*sizeof(Sample)];
struct Event { uint64_t at_ms; State from, to; uint32_t faults; bool ack, accepted; };
alignas(Event) uint8_t event_buffer[EVENT_QUEUE_LENGTH*sizeof(Event)];
struct Snapshot {
    Sample sample{};
    Output output{};
    uint32_t stale_tasks = 0;
    bool supervisor_inhibit = false;
};
Snapshot snapshot;
// Protects the diagnostic override and all GPIO writes, including other cores.
portMUX_TYPE actuator_mux = portMUX_INITIALIZER_UNLOCKED;
bool supervisor_inhibit = false;
uint32_t pending_trip = NO_FAULT;

void trip(uint32_t faults) {
    portENTER_CRITICAL(&actuator_mux);
    pending_trip |= faults;
    gpio_set_level(MOTOR,0);
    gpio_set_level(FAN,1);
    gpio_set_level(ALARM,1);
    portEXIT_CRITICAL(&actuator_mux);
}
void apply(const Output& out) {
    portENTER_CRITICAL(&actuator_mux);
    const bool inhibited = supervisor_inhibit || pending_trip != NO_FAULT;
    gpio_set_level(MOTOR, out.motor && !inhibited);
    gpio_set_level(FAN, out.fan || inhibited);
    gpio_set_level(ALARM, out.alarm || inhibited);
    portEXIT_CRITICAL(&actuator_mux);
}
uint32_t consume_trip() {
    portENTER_CRITICAL(&actuator_mux);
    const auto faults = pending_trip;
    pending_trip = NO_FAULT;
    portEXIT_CRITICAL(&actuator_mux);
    return faults;
}
void IRAM_ATTR acknowledge_isr(void*) {
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(handles[static_cast<size_t>(Task::Control)],ACK_EDGE,eSetBits,&woken);
    if (woken) portYIELD_FROM_ISR();
}
void beat(Task task, uint64_t now, uint64_t expected) {
    xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
    diagnostics.beat(task,now,now > expected ? now-expected : 0);
    xSemaphoreGive(snapshot_mutex);
}
void log_result(const Output& out, State previous, uint64_t now, bool ack) {
    if (!out.transition && !ack) return;
    Event event{now,previous,out.state,out.latched_faults,ack,out.ack_accepted};
    const bool sent = xQueueSend(events,&event,0) == pdPASS;
    xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
    diagnostics.record_queue_send(sent,false);
    if (out.transition && out.state == State::FAULT_LATCHED) ++diagnostics.counters.fault_entries;
    if (out.ack_rejected) ++diagnostics.counters.ack_rejections;
    xSemaphoreGive(snapshot_mutex);
}
void SensorAcquisitionTask(void*) {
    wait_for_start();
    TickType_t wake = xTaskGetTickCount();
    uint64_t expected = now_ms();
    while (true) {
        const auto now = now_ms();
        const Sample sample = sensors.read(now);
        bool sent = true;
        if (sample.publish) sent = xQueueSend(readings,&sample,0) == pdPASS;
        xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
        if (!sample.valid) ++diagnostics.counters.invalid_samples;
        diagnostics.record_queue_send(sent,true);
        xSemaphoreGive(snapshot_mutex);
        // Losing samples can conceal an unsafe transient. Fail closed on overflow.
        if (!sent) trip(TASK_HEALTH);
        beat(Task::Sensor,now_ms(),expected);
        expected += 100;
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(100));
    }
}
void ControlTask(void*) {
    wait_for_start();
    TickType_t wake = xTaskGetTickCount();
    uint64_t expected = now_ms(), previous_ms = expected;
    Sample latest{};
    bool has_sample = false;
    Output out{};
    ButtonDebouncer button(gpio_get_level(ACK) != 0);
    while (true) {
#ifdef CONFIG_INDUSTRIAL_DEMO_STALL
        if (gpio_get_level(GPIO_NUM_33) == 0) vTaskDelay(pdMS_TO_TICKS(1000));
#endif
        const int64_t started_us = esp_timer_get_time();
        const auto now = now_ms();
        uint32_t notification = 0;
        xTaskNotifyWait(0,UINT32_MAX,&notification,0);
        // Edge notification is a hint; periodic level verification debounces both
        // contacts and guarantees release rearming even if edges coalesce.
        (void)notification;
        const bool ack = button.update(gpio_get_level(ACK) != 0,now);
        xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
        const auto stale = diagnostics.stale_mask(now);
        xSemaphoreGive(snapshot_mutex);
        const auto pending = consume_trip();
        Input input{};
        input.now_ms = now;
        input.task_healthy = (stale & SAFETY_TASK_MASK) == 0 && !(pending & TASK_HEALTH);
        input.deadline_ok = now >= previous_ms && now - previous_ms <= 75 && !(pending & CONTROL_DEADLINE);
        previous_ms = now;
        Sample queued{};
        // Evaluate EVERY queued measurement so an unsafe transient cannot be
        // overwritten by a newer safe sample before the control task sees it.
        for (size_t i=0; i<SENSOR_QUEUE_LENGTH && xQueueReceive(readings,&queued,0)==pdPASS; ++i) {
            latest = queued;
            has_sample = true;
            input.sample = latest;
            input.has_sample = true;
            const State previous = out.state;
            out = controller.step(input);
            log_result(out,previous,now,false);
        }
        input.sample = latest;
        input.has_sample = has_sample;
        input.acknowledge = ack;
        const State previous = out.state;
        out = controller.step(input);
        apply(out);
        log_result(out,previous,now,ack);
        const uint64_t execution_us = static_cast<uint64_t>(esp_timer_get_time()-started_us);
        const uint64_t applied_ms = now_ms();
        bool inhibit;
        portENTER_CRITICAL(&actuator_mux);
        inhibit = supervisor_inhibit || pending_trip != 0;
        portEXIT_CRITICAL(&actuator_mux);
        xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
        snapshot.sample = latest;
        snapshot.output = out;
        snapshot.output.motor = out.motor && !inhibit;
        snapshot.supervisor_inhibit = inhibit;
        snapshot.stale_tasks = stale;
        diagnostics.counters.max_control_exec_us = std::max(diagnostics.counters.max_control_exec_us,execution_us);
        if (has_sample && applied_ms >= latest.timestamp_ms)
            diagnostics.counters.max_sample_to_output_ms = std::max(diagnostics.counters.max_sample_to_output_ms,applied_ms-latest.timestamp_ms);
        xSemaphoreGive(snapshot_mutex);
        beat(Task::Control,now_ms(),expected);
        expected += CONTROL_PERIOD_MS;
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
void DiagnosticsTask(void*) {
    wait_for_start();
    TickType_t wake = xTaskGetTickCount();
    uint64_t expected = now_ms();
    while (true) {
        const auto now = now_ms();
        xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
        diagnostics.beat(Task::Diagnostics,now,now > expected ? now-expected : 0);
        const auto stale = diagnostics.stale_mask(now);
        const bool deadline_ok = diagnostics.control_deadline_ok(now);
        for (size_t i=0;i<TASK_COUNT;++i)
            if (handles[i]) diagnostics.stack_free(static_cast<Task>(i),uxTaskGetStackHighWaterMark(handles[i]));
        xSemaphoreGive(snapshot_mutex);
        portENTER_CRITICAL(&actuator_mux);
        supervisor_inhibit = (stale & SAFETY_TASK_MASK) != 0 || !deadline_ok;
        if (supervisor_inhibit) {
            pending_trip |= ((stale & SAFETY_TASK_MASK) ? static_cast<uint32_t>(TASK_HEALTH) : 0u) | (!deadline_ok ? static_cast<uint32_t>(CONTROL_DEADLINE) : 0u);
            gpio_set_level(MOTOR,0);
            gpio_set_level(FAN,1);
            gpio_set_level(ALARM,1);
        }
        portEXIT_CRITICAL(&actuator_mux);
        expected += 100;
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(100));
    }
}
void TelemetryTask(void*) {
    wait_for_start();
    if (!network_initialize()) ESP_LOGW(TAG,"Network initialization failed; serial telemetry continues");
    TickType_t wake = xTaskGetTickCount();
    uint64_t expected = now_ms();
    while (true) {
        Snapshot copy;
        Counters counts;
        std::array<TaskStats,TASK_COUNT> stats;
        xSemaphoreTake(snapshot_mutex,portMAX_DELAY);
        copy = snapshot; counts = diagnostics.counters; stats = diagnostics.tasks();
        copy.stale_tasks = diagnostics.stale_mask(now_ms());
        xSemaphoreGive(snapshot_mutex);
        portENTER_CRITICAL(&actuator_mux);
        copy.supervisor_inhibit = supervisor_inhibit || pending_trip != NO_FAULT;
        if (copy.supervisor_inhibit) {
            copy.output.motor = false; copy.output.fan = true; copy.output.alarm = true;
            copy.output.active_faults |= pending_trip;
        }
        portEXIT_CRITICAL(&actuator_mux);
        Event event{};
        for (size_t i=0;i<EVENT_QUEUE_LENGTH && xQueueReceive(events,&event,0)==pdPASS;++i) {
            ESP_LOGI(TAG,"event at=%" PRIu64 " %s -> %s faults=0x%" PRIx32 " ack=%d accepted=%d",
                     event.at_ms,state_name(event.from),state_name(event.to),event.faults,event.ack,event.accepted);
        }
        const auto link = network_status();
        TelemetryFrame frame{};
        frame.now_ms=now_ms(); frame.sample=copy.sample; frame.output=copy.output;
        frame.counters=counts; frame.counters.telemetry_drops=link.dropped;
        frame.tasks=stats; frame.stale_tasks=copy.stale_tasks;
        frame.supervisor_inhibit=copy.supervisor_inhibit;
        frame.wifi_connected=link.wifi; frame.mqtt_connected=link.mqtt;
        char json[1536];
        if (format_json(frame,json,sizeof(json))) {
            ESP_LOGI(TAG,"%s",json);
            network_service(frame.now_ms,json);
        }
        beat(Task::Telemetry,now_ms(),expected);
        expected += 1000;
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
    }
}
}
extern "C" void app_main() {
    epoch_ms = static_cast<uint64_t>(esp_timer_get_time()/1000);
    gpio_config_t outputs{};
    outputs.pin_bit_mask = (1ULL << MOTOR) | (1ULL << FAN) | (1ULL << ALARM);
    outputs.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&outputs));
    gpio_set_level(MOTOR,0); gpio_set_level(FAN,0); gpio_set_level(ALARM,0);
    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << ACK;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    button.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&button));
#ifdef CONFIG_INDUSTRIAL_DEMO_STALL
    gpio_config_t stall{}; stall.pin_bit_mask=1ULL<<GPIO_NUM_33;
    stall.mode=GPIO_MODE_INPUT; stall.pull_up_en=GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&stall));
#endif
    snapshot_mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
    readings=xQueueCreateStatic(SENSOR_QUEUE_LENGTH,sizeof(Sample),sensor_buffer,&sensor_queue_storage);
    events=xQueueCreateStatic(EVENT_QUEUE_LENGTH,sizeof(Event),event_buffer,&event_queue_storage);
    configASSERT(snapshot_mutex && readings && events);
    if (!sensors.initialize()) {
        trip(INVALID_SAMPLE);
        ESP_LOGE(TAG,"Sensor initialization failed; motor disabled. Reset to retry initialization.");
        return;
    }
    ESP_LOGI(TAG,"IndustrialRTOS ESP-IDF: virtual equipment only; analog temperature/vibration, LED actuators");
    // Stack sizes are BYTES in ESP-IDF. Boot-only task allocations, no heap in control cycles.
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ControlTask,"ControlTask",4096,nullptr,5,&handles[1],1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorAcquisitionTask,"SensorAcquisition",3072,nullptr,6,&handles[0],1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(DiagnosticsTask,"DiagnosticsTask",3072,nullptr,7,&handles[2],1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(TelemetryTask,"TelemetryTask",6144,nullptr,2,&handles[3],1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ACK,acknowledge_isr,nullptr));
    epoch_ms = static_cast<uint64_t>(esp_timer_get_time()/1000);
    startup_ready.store(true);
}
