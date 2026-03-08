#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <curl/curl.h>

namespace hms_nvr {

struct CameraConfig {
    std::string id;          // e.g. "front_door"
    std::string name;        // e.g. "Front Door"
    std::string host;        // e.g. "192.168.2.43"
    std::string username;
    std::string password;
    std::string model;       // e.g. "Amcrest IP4M-1041"
    std::vector<std::string> events;  // e.g. ["VideoMotion", "PhoneCallDetect"]
    bool enabled = true;
};

struct CameraEvent {
    std::string camera_id;
    std::string event_code;   // "VideoMotion", "PhoneCallDetect"
    std::string action;       // "Start", "Stop"
    int index = 0;            // Event channel index
};

/**
 * AmcrestStream - Long-poll Amcrest event stream for a single camera
 *
 * Connects to the camera's CGI event API and streams events in real-time.
 * Runs in its own thread, calls back on motion start/stop.
 */
class AmcrestStream {
public:
    using EventCallback = std::function<void(const CameraEvent& event)>;

    explicit AmcrestStream(const CameraConfig& config);
    ~AmcrestStream();

    AmcrestStream(const AmcrestStream&) = delete;
    AmcrestStream& operator=(const AmcrestStream&) = delete;

    /// Start the event stream thread
    void start(EventCallback callback);

    /// Stop the event stream
    void stop();

    /// Check if stream is running and healthy
    bool isRunning() const { return running_.load(); }

    /// Check if stream is connected to camera
    bool isConnected() const { return connected_.load(); }

    /// Check if any event data has been received
    bool hasEvents() const { return has_events_.load(); }

    /// Get camera config
    const CameraConfig& config() const { return config_; }

    /// Get consecutive failure count
    int failureCount() const { return failure_count_.load(); }

private:
    /// Main loop: connect, read events, reconnect on failure
    void streamLoop();

    /// Parse a single event block from Amcrest's chunked response
    bool parseEventLine(const std::string& line, CameraEvent& event);

    /// Build the event stream URL with codes filter
    std::string buildUrl() const;

    /// libcurl write callback (receives chunked data)
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

    CameraConfig config_;
    EventCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> has_events_{false};
    std::atomic<int> failure_count_{0};

    // Buffer for partial lines from chunked transfer
    std::string line_buffer_;
    std::mutex buffer_mutex_;

    // Reconnect settings
    int reconnect_delay_ = 5;
    int connect_timeout_ = 10;

    // Last time data was received (for heartbeat tracking)
    std::chrono::steady_clock::time_point last_data_time_;

    // True if alert threshold was hit
    std::atomic<bool> gave_up_{false};
};

}  // namespace hms_nvr
