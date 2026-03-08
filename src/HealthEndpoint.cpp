#include "HealthEndpoint.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace hms_nvr {

HealthEndpoint::HealthEndpoint(int port, StatusCallback cb)
    : port_(port), status_cb_(std::move(cb)) {}

HealthEndpoint::~HealthEndpoint() {
    stop();
}

void HealthEndpoint::start() {
    running_ = true;
    thread_ = std::thread(&HealthEndpoint::listenLoop, this);
    std::cout << "[Health] Listening on port " << port_ << std::endl;
}

void HealthEndpoint::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void HealthEndpoint::listenLoop() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[Health] Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Health] Bind failed on port " << port_ << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    listen(server_fd_, 4);

    while (running_) {
        int client = accept(server_fd_, nullptr, nullptr);
        if (client < 0) break;

        // Read request (discard it — we always return health)
        char buf[1024];
        read(client, buf, sizeof(buf));

        std::string body = status_cb_ ? status_cb_() : R"({"status":"ok"})";
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        write(client, response.c_str(), response.size());
        close(client);
    }
}

}  // namespace hms_nvr
