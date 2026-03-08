#include "ConfigLoader.h"
#include "mqtt/MqttClient.h"
#include "amcrest/NvrManager.h"
#include "HealthEndpoint.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <curl/curl.h>

using namespace hms_nvr;

static std::unique_ptr<NvrManager> g_nvr;
static std::shared_ptr<MqttClient> g_mqtt;
static std::unique_ptr<HealthEndpoint> g_health;
static std::atomic<bool> g_running{true};
static std::condition_variable g_cv;
static std::mutex g_mutex;

void signalHandler(int signal) {
    std::cout << "\n[HMS-NVR] Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    g_cv.notify_all();
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  HMS-NVR v1.1.0" << std::endl;
    std::cout << "  Amcrest Event Stream -> MQTT" << std::endl;
    std::cout << "========================================" << std::endl;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    curl_global_init(CURL_GLOBAL_ALL);

    // Find config file
    std::string config_path = "config/hms-nvr.yaml";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[i + 1];
            break;
        }
    }

    try {
        std::cout << "Loading config: " << config_path << std::endl;
        AppConfig config = ConfigLoader::load(config_path);

        std::cout << "\nConfiguration:" << std::endl;
        std::cout << "  MQTT: " << config.mqtt_broker << ":" << config.mqtt_port << std::endl;
        std::cout << "  Cameras: " << config.cameras.size() << std::endl;
        for (const auto& cam : config.cameras) {
            std::cout << "    - " << cam.id << " (" << cam.name << ") @ " << cam.host;
            if (!cam.model.empty()) std::cout << " [" << cam.model << "]";
            std::cout << " events=[";
            for (size_t i = 0; i < cam.events.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << cam.events[i];
            }
            std::cout << "]";
            if (!cam.enabled) std::cout << " DISABLED";
            std::cout << std::endl;
        }
        std::cout << std::endl;

        // Initialize MQTT
        g_mqtt = std::make_shared<MqttClient>(config.mqtt_client_id);
        std::string broker_url = "tcp://" + config.mqtt_broker + ":" + std::to_string(config.mqtt_port);
        if (!g_mqtt->connect(broker_url, config.mqtt_username, config.mqtt_password)) {
            std::cerr << "Failed to connect to MQTT broker" << std::endl;
            curl_global_cleanup();
            return 1;
        }

        // Initialize and start NVR
        g_nvr = std::make_unique<NvrManager>(g_mqtt, config.cameras, config.publish_motion_stop);
        g_nvr->start();

        // Health endpoint
        g_health = std::make_unique<HealthEndpoint>(config.port, [&]() {
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return Json::writeString(w, g_nvr->getStatus());
        });
        g_health->start();

        std::cout << "HMS-NVR running. Health: http://0.0.0.0:" << config.port << "/health" << std::endl;

        // Block main thread until signal
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return !g_running.load(); });
        }

        // Graceful shutdown
        if (g_health) g_health->stop();
        g_nvr->stop();
        g_mqtt->disconnect();

        std::cout << "HMS-NVR shut down successfully" << std::endl;
        curl_global_cleanup();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        curl_global_cleanup();
        return 1;
    }
}
