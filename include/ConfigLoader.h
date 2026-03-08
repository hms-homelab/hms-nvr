#pragma once

#include "amcrest/AmcrestStream.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

namespace hms_nvr {

struct AppConfig {
    // Server
    int port = 8896;
    int threads = 2;

    // MQTT
    std::string mqtt_broker = "192.168.2.15";
    int mqtt_port = 1883;
    std::string mqtt_username = "aamat";
    std::string mqtt_password = "exploracion";
    std::string mqtt_client_id = "hms_nvr";

    // Cameras
    std::vector<CameraConfig> cameras;

    // Event stream
    int reconnect_delay = 5;
    int connect_timeout = 10;
    int heartbeat_interval = 30;
    bool publish_motion_stop = true;

    // Logging
    std::string log_level = "info";
};

class ConfigLoader {
public:
    static AppConfig load(const std::string& path);
};

}  // namespace hms_nvr
