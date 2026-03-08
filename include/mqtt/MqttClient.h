#pragma once

#include <mqtt/async_client.h>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <map>

namespace hms_nvr {

class MqttClient {
public:
    using MessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;

    explicit MqttClient(const std::string& client_id);
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    bool connect(const std::string& broker_address,
                 const std::string& username,
                 const std::string& password);
    void disconnect();
    bool isConnected() const;

    bool publish(const std::string& topic, const std::string& payload,
                 int qos = 1, bool retain = false);

private:
    void onConnectionLost(const std::string& cause);
    void onReconnected(const std::string& cause);

    std::unique_ptr<mqtt::async_client> client_;
    std::string client_id_;
    std::string broker_address_;
    std::string username_;
    std::string password_;
    bool connected_ = false;
    bool initial_connect_done_ = false;
    mutable std::recursive_mutex connection_mutex_;
};

}  // namespace hms_nvr
