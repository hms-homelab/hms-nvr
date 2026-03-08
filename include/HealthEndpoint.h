#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace hms_nvr {

class HealthEndpoint {
public:
    using StatusCallback = std::function<std::string()>;

    HealthEndpoint(int port, StatusCallback cb);
    ~HealthEndpoint();

    void start();
    void stop();

private:
    void listenLoop();

    int port_;
    StatusCallback status_cb_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int server_fd_ = -1;
};

}  // namespace hms_nvr
