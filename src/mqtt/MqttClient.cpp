#include "mqtt/MqttClient.h"
#include <iostream>

namespace hms_nvr {

MqttClient::MqttClient(const std::string& client_id)
    : client_id_(client_id) {
    std::cout << "[MQTT] Initialized client: " << client_id << std::endl;
}

MqttClient::~MqttClient() {
    disconnect();
}

bool MqttClient::connect(const std::string& broker_address,
                          const std::string& username,
                          const std::string& password) {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    broker_address_ = broker_address;
    username_ = username;
    password_ = password;

    try {
        std::string full_id = client_id_ + "_" + std::to_string(std::time(nullptr));
        client_ = std::make_unique<mqtt::async_client>(broker_address, full_id);

        client_->set_connection_lost_handler([this](const std::string& cause) {
            onConnectionLost(cause);
        });
        client_->set_connected_handler([this](const std::string& cause) {
            if (initial_connect_done_) onReconnected(cause);
        });

        mqtt::connect_options opts;
        opts.set_keep_alive_interval(60);
        opts.set_clean_session(true);
        opts.set_user_name(username);
        opts.set_password(password);
        opts.set_automatic_reconnect(1, 64);

        client_->connect(opts)->wait();
        connected_ = true;
        initial_connect_done_ = true;
        std::cout << "[MQTT] Connected to " << broker_address << std::endl;
        return true;

    } catch (const mqtt::exception& e) {
        std::cerr << "[MQTT] Connection failed: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void MqttClient::disconnect() {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    if (client_ && connected_) {
        try {
            client_->disconnect()->wait();
            connected_ = false;
            std::cout << "[MQTT] Disconnected" << std::endl;
        } catch (const mqtt::exception& e) {
            std::cerr << "[MQTT] Disconnect error: " << e.what() << std::endl;
        }
    }
}

bool MqttClient::isConnected() const {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    return connected_ && client_ && client_->is_connected();
}

bool MqttClient::publish(const std::string& topic, const std::string& payload,
                          int qos, bool retain) {
    if (!isConnected()) {
        std::cerr << "[MQTT] Not connected, cannot publish" << std::endl;
        return false;
    }
    try {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(qos);
        msg->set_retained(retain);
        {
            std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
            client_->publish(msg);
        }
        return true;
    } catch (const mqtt::exception& e) {
        std::cerr << "[MQTT] Publish failed: " << e.what() << std::endl;
        return false;
    }
}

void MqttClient::onConnectionLost(const std::string& cause) {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    connected_ = false;
    std::cerr << "[MQTT] Connection lost: " << cause << std::endl;
}

void MqttClient::onReconnected(const std::string& /*cause*/) {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    connected_ = true;
    std::cout << "[MQTT] Reconnected" << std::endl;
}

}  // namespace hms_nvr
