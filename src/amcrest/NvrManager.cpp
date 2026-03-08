#include "amcrest/NvrManager.h"
#include <json/json.h>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace hms_nvr {

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

static std::string toJson(const Json::Value& val) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, val);
}

NvrManager::NvrManager(std::shared_ptr<MqttClient> mqtt,
                       const std::vector<CameraConfig>& cameras,
                       bool publish_motion_stop)
    : mqtt_(std::move(mqtt)), camera_configs_(cameras), publish_motion_stop_(publish_motion_stop) {

    for (const auto& cam : camera_configs_) {
        if (!cam.enabled) {
            std::cout << "[NVR] Skipping disabled camera: " << cam.id << std::endl;
            continue;
        }
        streams_.push_back(std::make_unique<AmcrestStream>(cam));
        motion_state_[cam.id] = false;
    }
}

NvrManager::~NvrManager() {
    stop();
}

void NvrManager::start() {
    mqtt_->publish("hms_nvr/status", "online", 1, true);

    for (auto& stream : streams_) {
        stream->start([this](const CameraEvent& event) {
            onCameraEvent(event);
        });
    }

    std::cout << "[NVR] Started " << streams_.size() << " camera streams" << std::endl;
}

void NvrManager::stop() {
    for (auto& stream : streams_) {
        stream->stop();
    }

    if (mqtt_ && mqtt_->isConnected()) {
        mqtt_->publish("hms_nvr/status", "offline", 1, true);
    }

    std::cout << "[NVR] All streams stopped" << std::endl;
}

void NvrManager::onCameraEvent(const CameraEvent& event) {
    Json::Value payload;
    payload["camera_id"] = event.camera_id;

    if (event.event_code == "VideoMotion") {
        std::string topic;
        bool should_publish = false;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            bool currently_active = motion_state_[event.camera_id];

            if (event.action == "Start" && !currently_active) {
                motion_state_[event.camera_id] = true;
                topic = "camera/event/motion/start";
                should_publish = true;
            } else if (event.action == "Stop" && currently_active) {
                motion_state_[event.camera_id] = false;
                if (publish_motion_stop_) {
                    topic = "camera/event/motion/stop";
                    should_publish = true;
                }
            }
            // Duplicate start/stop is suppressed (no log spam)
        }

        if (should_publish) {
            std::string json_str = toJson(payload);
            mqtt_->publish(topic, json_str, 1, false);
            std::cout << timestamp() << " [NVR] -> " << topic << " " << json_str << std::endl;
        }

    } else if (event.event_code == "PhoneCallDetect") {
        if (event.action == "Start") {
            std::string json_str = toJson(payload);
            mqtt_->publish("camera/event/doorbell", json_str, 1, false);
            std::cout << timestamp() << " [NVR] -> camera/event/doorbell " << json_str << std::endl;
        }

    } else if (event.event_code == "ConnectionFailure") {
        // Camera gave up after MAX_FAILURES_BEFORE_ALERT consecutive failures
        // Publish alert to MQTT so n8n can pick it up and notify
        payload["event"] = "connection_failure";
        payload["failures"] = event.index;
        payload["message"] = "Camera " + event.camera_id + " unreachable after "
                           + std::to_string(event.index) + " consecutive failures";

        std::string json_str = toJson(payload);
        mqtt_->publish("hms_nvr/alert/camera_down", json_str, 1, true);
        std::cerr << timestamp() << " [NVR] ALERT published: hms_nvr/alert/camera_down "
                  << json_str << std::endl;
    }
}

Json::Value NvrManager::getStatus() const {
    Json::Value status;
    status["service"] = "hms-nvr";
    status["version"] = "1.0.0";

    Json::Value cameras(Json::arrayValue);
    for (const auto& stream : streams_) {
        Json::Value cam;
        cam["id"] = stream->config().id;
        cam["name"] = stream->config().name;
        cam["model"] = stream->config().model;
        cam["connected"] = stream->isConnected();
        cam["has_events"] = stream->hasEvents();
        cam["running"] = stream->isRunning();
        cam["failures"] = stream->failureCount();
        cameras.append(cam);
    }
    status["cameras"] = cameras;
    status["mqtt_connected"] = mqtt_ ? mqtt_->isConnected() : false;

    return status;
}

}  // namespace hms_nvr
