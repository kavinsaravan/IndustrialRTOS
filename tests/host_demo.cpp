#include "control.hpp"
#include "telemetry.hpp"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
using namespace industrial;
class Demo {
public:
    float temperature=40,vibration=1;
    bool valid=true,sampling=true,healthy=true,network=false;
    uint64_t now=0;
    void tick(bool ack=false,bool force=false) {
        if (sampling && now%100==0) { sample={now,temperature,vibration,valid,true}; has_sample=true; }
        Input in{}; in.now_ms=now; in.sample=sample; in.has_sample=has_sample;
        in.task_healthy=healthy; in.acknowledge=ack;
        auto out=controller.step(in);
        if (force || out.transition || ack || now%1000==0) {
            TelemetryFrame frame{}; frame.now_ms=now;frame.sample=sample;frame.output=out;
            frame.wifi_connected=network;frame.mqtt_connected=network;
            char json[1536]; if (format_json(frame,json,sizeof(json))) std::puts(json);
        }
        now+=50;
    }
    void advance(uint64_t duration) { for(uint64_t i=0;i<duration;i+=50) tick(); }
private:
    Controller controller;
    Sample sample{};
    bool has_sample=false;
};
int main(int argc,char** argv) {
    Demo demo;
    if(argc==2 && std::string(argv[1])=="--interactive") {
        std::cerr<<"Native logic demo (virtual time; no ESP32/FreeRTOS emulation).\n"
                 <<"Commands: temp N | vibration N | invalid on/off | samples on/off | heartbeat on/off | network on/off | ack | advance MS | quit\n";
        demo.tick(false,true);
        std::string line;
        while(std::getline(std::cin,line)) {
            std::istringstream input(line); std::string command,arg;input>>command;
            if(command=="quit") break;
            if(command=="temp") {if(!(input>>demo.temperature)) {std::cerr<<"Expected numeric temperature\n";continue;}}
            else if(command=="vibration") {if(!(input>>demo.vibration)) {std::cerr<<"Expected numeric vibration\n";continue;}}
            else if(command=="advance") {uint64_t duration;if(!(input>>duration)||duration>60000) {std::cerr<<"Use 0..60000 ms\n";continue;}demo.advance(duration);continue;}
            else if(command=="ack") {demo.tick(true,true);continue;}
            else if(command=="invalid" || command=="samples" || command=="heartbeat" || command=="network") {
                input>>arg;if(arg!="on" && arg!="off") {std::cerr<<"Expected on/off\n";continue;}
                const bool on=arg=="on";
                if(command=="invalid") demo.valid=!on;
                if(command=="samples") demo.sampling=on;
                if(command=="heartbeat") demo.healthy=on;
                if(command=="network") demo.network=on;
            } else {std::cerr<<"Unknown command\n";continue;}
            demo.tick(false,true);
        }
    } else {
        std::cerr<<"Native logic walkthrough: virtual time, synthetic samples, no hardware timing measurements.\n";
        demo.advance(1000);
        demo.temperature=68;demo.advance(1000);
        demo.temperature=85;demo.advance(1000);
        demo.tick(true,true); // Unsafe acknowledgment rejected.
        demo.temperature=40;demo.advance(3500);
        demo.tick(true,true); // New safe acknowledgment accepted.
        demo.sampling=false;demo.advance(1000); // Freshness fault.
        demo.sampling=true;demo.advance(3500);demo.tick(true,true);
        demo.vibration=8;demo.advance(500);
    }
}
