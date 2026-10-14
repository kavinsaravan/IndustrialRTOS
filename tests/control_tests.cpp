#include "control.hpp"
#include "diagnostics.hpp"
#include "telemetry.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include <string_view>
#include <cstring>
using namespace industrial;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); std::exit(1); } } while (false)
static int passed = 0;
template<typename F> void test(const char* name, F f) { f(); ++passed; std::printf("PASS %s\n", name); }
Input reading(uint64_t now, float temp = 40, float vibration = 1, bool ack = false) {
    Input in{}; in.now_ms = now; in.has_sample = true;
    in.sample = {now, temp, vibration, true, true}; in.acknowledge = ack; return in;
}
Output hold_safe(Controller& c, uint64_t start, uint64_t duration) {
    Output out{};
    for (uint64_t now = start; now <= start + duration; now += 50) out = c.step(reading(now));
    return out;
}
int main() {
    test("boot waits for valid initialization", [] {
        Controller c; Input in{}; CHECK(c.step(in).state == State::INIT);
        auto out = c.step(reading(100)); CHECK(out.state == State::NORMAL && out.motor);
    });
    test("failed sensor initialization inhibits motor", [] {
        Controller c; auto in = reading(0); in.initialized = false;
        auto out = c.step(in); CHECK(out.active_faults & INVALID_SAMPLE); CHECK(!out.motor);
    });
    test("normal conditions and fan hysteresis", [] {
        Controller c; CHECK(c.step(reading(0)).motor);
        CHECK(c.step(reading(50,55)).fan); CHECK(c.step(reading(100,52)).fan);
        CHECK(!c.step(reading(150,49)).fan);
    });
    test("warning threshold is inclusive", [] {
        Controller c; auto out = c.step(reading(0,65));
        CHECK(out.state == State::WARNING && out.motor && out.fan);
    });
    test("critical temperature disables motor", [] {
        Controller c; auto out = c.step(reading(0,80));
        CHECK(out.state == State::FAULT_LATCHED && !out.motor && out.fan && out.alarm);
        CHECK(out.active_faults == TEMPERATURE);
    });
    test("vibration threshold disables motor", [] {
        Controller c; CHECK(c.step(reading(0,40,7)).active_faults == VIBRATION);
    });
    test("invalid and non-finite readings fail closed", [] {
        for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 101.0f, 19.0f}) {
            Controller c; CHECK(c.step(reading(0,value)).active_faults & INVALID_SAMPLE);
        }
        Controller c; auto in = reading(0); in.sample.valid = false;
        CHECK(c.step(in).active_faults & INVALID_SAMPLE);
    });
    test("freshness boundary and timeout", [] {
        Controller c; c.step(reading(0)); auto in = reading(350); in.sample.timestamp_ms = 0;
        CHECK(c.step(in).motor); in.now_ms = 351;
        auto out = c.step(in); CHECK(out.active_faults & SENSOR_TIMEOUT); CHECK(!out.motor);
    });
    test("no samples at boot times out", [] {
        Controller c; Input in{}; in.now_ms = 351; CHECK(c.step(in).active_faults & SENSOR_TIMEOUT);
    });
    test("future sample timestamp fails closed", [] {
        Controller c; auto in = reading(0); in.sample.timestamp_ms = 1;
        CHECK(c.step(in).active_faults & SENSOR_TIMEOUT);
    });
    test("warning hysteresis clears strictly below 62", [] {
        Controller c; c.step(reading(0,65));
        CHECK(c.step(reading(50,64)).state == State::WARNING);
        CHECK(c.step(reading(100,62)).state == State::WARNING);
        CHECK(c.step(reading(150,61.9f)).state == State::NORMAL);
    });
    test("unsafe acknowledgment never restarts", [] {
        Controller c; c.step(reading(0,90));
        auto out = c.step(reading(50,90,8,true));
        CHECK(out.state == State::FAULT_LATCHED && !out.motor && out.ack_rejected);
    });
    test("premature safe acknowledgment is not remembered", [] {
        Controller c; c.step(reading(0,90));
        CHECK(c.step(reading(100,40,1,true)).ack_rejected);
        auto out = hold_safe(c,150,3000);
        CHECK(out.state == State::RECOVERY_READY && !out.motor);
        CHECK(!c.step(reading(3200)).motor);
    });
    test("recovery requires dwell and a new acknowledgment", [] {
        Controller c; c.step(reading(0,90));
        CHECK(hold_safe(c,100,2950).state == State::FAULT_LATCHED);
        auto out = c.step(reading(3100)); CHECK(out.state == State::RECOVERY_READY && !out.motor);
        out = c.step(reading(3150,40,1,true));
        CHECK(out.state == State::NORMAL && out.motor && out.ack_accepted && out.latched_faults == 0);
    });
    test("ack at exact ready transition is rejected", [] {
        Controller c; c.step(reading(0,90)); hold_safe(c,100,2950);
        auto out = c.step(reading(3100,40,1,true));
        CHECK(out.state == State::RECOVERY_READY && !out.motor && out.ack_rejected);
    });
    test("unsafe interval resets recovery dwell", [] {
        Controller c; c.step(reading(0,90)); hold_safe(c,100,2000);
        c.step(reading(2150,60));
        CHECK(hold_safe(c,2200,2950).state == State::FAULT_LATCHED);
        CHECK(c.step(reading(5200)).state == State::RECOVERY_READY);
    });
    test("recovery ready revokes on unsafe measurement", [] {
        Controller c; c.step(reading(0,90)); hold_safe(c,100,3000);
        auto out = c.step(reading(3150,61,1,true));
        CHECK(out.state == State::FAULT_LATCHED && !out.motor && out.ack_rejected);
    });
    test("missed heartbeat and deadline are safety interlocks", [] {
        Controller c; auto in = reading(0); in.task_healthy = false; in.deadline_ok = false;
        auto out = c.step(in); CHECK(out.active_faults == (TASK_HEALTH | CONTROL_DEADLINE)); CHECK(!out.motor);
    });
    test("simultaneous faults preserved with deterministic priority", [] {
        Controller c; auto out = c.step(reading(0,90,8));
        CHECK(out.active_faults == (TEMPERATURE | VIBRATION));
        CHECK(primary_fault(out.active_faults) == std::string_view("TEMPERATURE"));
        auto in = reading(50,90,8); in.task_healthy = false;
        CHECK(primary_fault(c.step(in).active_faults) == std::string_view("TASK_HEALTH"));
    });
    test("latched fault mask accumulates until acknowledged", [] {
        Controller c; c.step(reading(0,90)); auto out = c.step(reading(50,40,8));
        CHECK(out.latched_faults == (TEMPERATURE | VIBRATION));
    });
    test("button bounce and held button generate only one press", [] {
        ButtonDebouncer button;
        CHECK(!button.update(false,0)); CHECK(!button.update(true,10));
        CHECK(!button.update(false,20)); CHECK(!button.update(false,69));
        CHECK(button.update(false,70)); CHECK(!button.update(false,4000));
        CHECK(!button.update(true,4050)); CHECK(!button.update(true,4100));
        CHECK(!button.update(false,4150)); CHECK(button.update(false,4200));
    });
    test("boot-held button requires release and new press", [] {
        ButtonDebouncer button(false); CHECK(!button.update(false,100));
        CHECK(!button.update(true,150)); CHECK(!button.update(true,200));
        CHECK(!button.update(false,250)); CHECK(button.update(false,300));
    });

    test("diagnostic heartbeat timeout trips controller", [] {
        Diagnostics d;
        for (size_t i=0; i<TASK_COUNT; ++i) d.beat(static_cast<Task>(i),0);
        CHECK(d.stale_mask(150)==0);
        CHECK(d.stale_mask(151)==(1u<<static_cast<size_t>(Task::Control)));
        auto in=reading(151); in.task_healthy=(d.stale_mask(151)&7u)==0;
        Controller c; CHECK(!c.step(in).motor);
    });
    test("control deadline supervision is independent of controller", [] {
        Diagnostics d; d.beat(Task::Control,100);
        CHECK(d.control_deadline_ok(175)); CHECK(!d.control_deadline_ok(176));
        CHECK(!d.control_deadline_ok(99));
    });
    test("diagnostic startup grace is bounded", [] {
        Diagnostics d; CHECK(d.stale_mask(0)==0); CHECK(d.stale_mask(151)&2u);
        CHECK(d.stale_mask(301)==7u); CHECK(d.stale_mask(3001)==15u);
    });
    test("queue overflow counters count failed sends only", [] {
        Diagnostics d; d.record_queue_send(true,true); CHECK(d.counters.sensor_queue_overflows==0);
        d.record_queue_send(false,true); d.record_queue_send(false,true);
        d.record_queue_send(false,false); CHECK(d.counters.sensor_queue_overflows==2);
        CHECK(d.counters.event_queue_overflows==1);
        // Same conservative interlock used by the sensor overflow adapter.
        auto in=reading(0); in.task_healthy=false; Controller c;
        CHECK(c.step(in).active_faults&TASK_HEALTH);
    });
    test("deadline and jitter counters reflect late beats", [] {
        Diagnostics d; d.beat(Task::Control,0); d.beat(Task::Control,50,0);
        d.beat(Task::Control,140,40); const auto stats=d.tasks()[1];
        CHECK(stats.beats==3 && stats.missed_deadlines==1 && stats.max_jitter_ms==40);
    });
    test("MQTT disconnect leaves normal and fault control operational", [] {
        Controller connected,disconnected;
        TelemetryFrame online{},offline{};
        online.wifi_connected=true; online.mqtt_connected=true;
        char online_json[1536],offline_json[1536];
        for (uint64_t now=0; now<=500; now+=50) {
            auto in=reading(now,now>=250 ? 85.0f : 40.0f);
            online.output=connected.step(in); offline.output=disconnected.step(in);
            online.sample=offline.sample=in.sample;
            CHECK(format_json(online,online_json,sizeof(online_json)));
            CHECK(format_json(offline,offline_json,sizeof(offline_json)));
            CHECK(online.output.state==offline.output.state);
            CHECK(online.output.motor==offline.output.motor);
            CHECK(offline.output.motor==(now<250));
        }
        // Tests the production pure control/formatting boundary, not Wi-Fi sockets.
        CHECK(std::strstr(offline_json,"\"mqtt\":false")!=nullptr);
    });
    test("telemetry heartbeat loss is advisory, critical tasks still monitored", [] {
        Diagnostics d;
        for (size_t i=0;i<3;++i) d.beat(static_cast<Task>(i),4000);
        CHECK(d.stale_mask(4000)==8u);
        auto in=reading(4000); in.task_healthy=(d.stale_mask(4000)&7u)==0;
        Controller c; CHECK(c.step(in).motor);
    });
    test("bounded reconnect doubles to 30 seconds and resets", [] {
        RetryBackoff retry; CHECK(retry.attempt(0)); CHECK(retry.next_ms()==1000);
        CHECK(!retry.attempt(999)); CHECK(retry.attempt(1000)); CHECK(retry.next_ms()==3000);
        uint64_t now=3000;
        for (int i=0;i<10;++i) {
            CHECK(retry.attempt(now)); CHECK(retry.next_ms()-now<=30000);
            if (i>=4) CHECK(retry.next_ms()-now==30000);
            now=retry.next_ms();
        }
        retry.connected(); CHECK(retry.attempt(now)); CHECK(retry.next_ms()==now+1000);
    });
    test("JSON formatter rejects truncated messages", [] {
        TelemetryFrame f; char tiny[8]; CHECK(!format_json(f,tiny,sizeof(tiny))); CHECK(tiny[0]=='\0');
        CHECK(!format_json(f,nullptr,0)); char buffer[1536]; CHECK(format_json(f,buffer,sizeof(buffer)));
        CHECK(buffer[0]=='{' && buffer[std::strlen(buffer)-1]=='}');
    });
    test("invalid measurements serialize as JSON null", [] {
        TelemetryFrame f; f.sample.valid=false; f.sample.temperature_c=std::numeric_limits<float>::quiet_NaN();
        char buffer[1536]; CHECK(format_json(f,buffer,sizeof(buffer)));
        CHECK(std::strstr(buffer,"\"temperature_c\":null")); CHECK(std::strstr(buffer,"nan")==nullptr);
    });
    test("critical transient stays latched after newer safe reading", [] {
        Controller c; c.step(reading(0)); c.step(reading(100,90));
        auto out=c.step(reading(150)); CHECK(out.state==State::FAULT_LATCHED && !out.motor);
    });
    test("recovery does not progress using an indefinitely stale safe sample", [] {
        Controller c; c.step(reading(0,90)); c.step(reading(100));
        auto in=reading(3100); in.sample.timestamp_ms=100; in.acknowledge=true;
        auto out=c.step(in); CHECK(out.state==State::FAULT_LATCHED && !out.motor);
        CHECK(out.active_faults&SENSOR_TIMEOUT);
    });
    test("motor interlock invariant over deterministic mixed inputs", [] {
        Controller c; uint32_t seed=42; unsigned motor_steps=0,fault_steps=0,restarts=0;
        for (uint64_t now=0;now<100000;now+=50) {
            seed=1664525u*seed+1013904223u;
            auto in=reading(now,20.0f+static_cast<float>(seed%81),static_cast<float>((seed>>8)%11),seed%7==0);
            in.sample.valid=seed%19!=0; in.task_healthy=seed%23!=0; in.deadline_ok=seed%29!=0;
            const uint64_t phase=(now/50)%200;
            if (phase>=80) {
                in=reading(now,40,1,phase>140 && phase%7==0);
            }
            auto out=c.step(in);
            if (out.ack_accepted) ++restarts;
            if (out.motor) {
                ++motor_steps;
                CHECK(out.state==State::NORMAL || out.state==State::WARNING);
                CHECK(out.active_faults==0 && out.latched_faults==0);
                CHECK(in.sample.valid && in.task_healthy && in.deadline_ok);
                CHECK(in.sample.temperature_c<80 && in.sample.vibration<7);
            }
            if (out.state==State::FAULT_LATCHED || out.state==State::RECOVERY_READY) {
                ++fault_steps; CHECK(!out.motor && out.alarm);
            }
        }
        CHECK(motor_steps>50 && fault_steps>50 && restarts>5);
    });
    std::printf("%d tests passed\n",passed);
}
