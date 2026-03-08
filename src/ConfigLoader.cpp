#include "ConfigLoader.h"
#include <iostream>
#include <stdexcept>

namespace hms_nvr {

AppConfig ConfigLoader::load(const std::string& path) {
    AppConfig config;

    YAML::Node yaml = YAML::LoadFile(path);

    // Server
    if (yaml["server"]) {
        config.port = yaml["server"]["port"].as<int>(config.port);
        config.threads = yaml["server"]["threads"].as<int>(config.threads);
    }

    // MQTT
    if (yaml["mqtt"]) {
        config.mqtt_broker = yaml["mqtt"]["broker"].as<std::string>(config.mqtt_broker);
        config.mqtt_port = yaml["mqtt"]["port"].as<int>(config.mqtt_port);
        config.mqtt_username = yaml["mqtt"]["username"].as<std::string>(config.mqtt_username);
        config.mqtt_password = yaml["mqtt"]["password"].as<std::string>(config.mqtt_password);
        config.mqtt_client_id = yaml["mqtt"]["client_id"].as<std::string>(config.mqtt_client_id);
    }

    // Cameras
    if (yaml["cameras"]) {
        for (const auto& cam_node : yaml["cameras"]) {
            CameraConfig cam;
            cam.id = cam_node["id"].as<std::string>();
            cam.name = cam_node["name"].as<std::string>(cam.id);
            cam.host = cam_node["host"].as<std::string>();
            cam.username = cam_node["username"].as<std::string>("admin");
            cam.password = cam_node["password"].as<std::string>();
            cam.model = cam_node["model"].as<std::string>("");
            cam.enabled = cam_node["enabled"].as<bool>(true);

            if (cam_node["events"]) {
                for (const auto& ev : cam_node["events"]) {
                    cam.events.push_back(ev.as<std::string>());
                }
            } else {
                cam.events.push_back("VideoMotion");  // Default
            }

            config.cameras.push_back(std::move(cam));
        }
    }

    if (config.cameras.empty()) {
        throw std::runtime_error("No cameras configured");
    }

    // Event stream settings
    if (yaml["event_stream"]) {
        config.reconnect_delay = yaml["event_stream"]["reconnect_delay"].as<int>(config.reconnect_delay);
        config.connect_timeout = yaml["event_stream"]["connect_timeout"].as<int>(config.connect_timeout);
        config.heartbeat_interval = yaml["event_stream"]["heartbeat_interval"].as<int>(config.heartbeat_interval);
        config.publish_motion_stop = yaml["event_stream"]["publish_motion_stop"].as<bool>(config.publish_motion_stop);
    }

    // Logging
    if (yaml["logging"]) {
        config.log_level = yaml["logging"]["level"].as<std::string>(config.log_level);
    }

    return config;
}

}  // namespace hms_nvr
