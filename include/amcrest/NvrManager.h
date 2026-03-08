#pragma once

#include "amcrest/AmcrestStream.h"
#include "mqtt/MqttClient.h"
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <json/json.h>

namespace hms_nvr {

/**
 * NvrManager - Manages all camera streams and publishes events to MQTT
 *
 * Owns camera streams and MQTT client. Translates Amcrest events to
 * the MQTT topic contract expected by hms-detection:
 *   camera/event/motion/start  {"camera_id":"front_door"}
 *   camera/event/motion/stop   {"camera_id":"front_door"}
 *   camera/event/doorbell      {"camera_id":"front_door"}
 */
class NvrManager {
public:
    NvrManager(std::shared_ptr<MqttClient> mqtt,
               const std::vector<CameraConfig>& cameras,
               bool publish_motion_stop = true);
    ~NvrManager();

    /// Start all camera streams
    void start();

    /// Stop all camera streams
    void stop();

    /// Get status of all cameras (for health endpoint)
    Json::Value getStatus() const;

private:
    void onCameraEvent(const CameraEvent& event);

    std::shared_ptr<MqttClient> mqtt_;
    std::vector<std::unique_ptr<AmcrestStream>> streams_;
    std::vector<CameraConfig> camera_configs_;

    // Track motion state per camera to avoid duplicate publishes
    std::map<std::string, bool> motion_state_;
    std::mutex state_mutex_;
    bool publish_motion_stop_ = true;
};

}  // namespace hms_nvr
