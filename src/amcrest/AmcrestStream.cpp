#include "amcrest/AmcrestStream.h"
#include <iostream>
#include <sstream>
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

AmcrestStream::AmcrestStream(const CameraConfig& config)
    : config_(config) {}

AmcrestStream::~AmcrestStream() {
    stop();
}

void AmcrestStream::start(EventCallback callback) {
    if (running_.load()) return;

    callback_ = std::move(callback);
    running_ = true;
    thread_ = std::thread(&AmcrestStream::streamLoop, this);
    std::cout << timestamp() << " [" << config_.id
              << "] Event stream thread started (events: ";
    for (size_t i = 0; i < config_.events.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << config_.events[i];
    }
    std::cout << ")" << std::endl;
}

void AmcrestStream::stop() {
    if (!running_.load()) return;
    running_ = false;
    std::cout << timestamp() << " [" << config_.id << "] Requesting stream stop..." << std::endl;
    if (thread_.joinable()) {
        thread_.join();
    }
    connected_ = false;
    std::cout << timestamp() << " [" << config_.id << "] Event stream stopped cleanly" << std::endl;
}

std::string AmcrestStream::buildUrl() const {
    // URL-encode brackets: [ = %5B, ] = %5D (curl treats [] as glob ranges)
    std::string codes = "%5B";
    for (size_t i = 0; i < config_.events.size(); ++i) {
        if (i > 0) codes += ",";
        codes += config_.events[i];
    }
    codes += "%5D";
    return "http://" + config_.host +
           "/cgi-bin/eventManager.cgi?action=attach&codes=" + codes +
           "&heartbeat=5";
}

size_t AmcrestStream::writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<AmcrestStream*>(userdata);
    if (!stream->running_.load()) return 0;  // Abort transfer

    size_t total = size * nmemb;
    std::string chunk(ptr, total);

    stream->last_data_time_ = std::chrono::steady_clock::now();
    if (!stream->connected_.load()) {
        stream->connected_ = true;
        stream->failure_count_ = 0;
        stream->gave_up_ = false;
    }

    std::lock_guard<std::mutex> lock(stream->buffer_mutex_);
    stream->line_buffer_ += chunk;

    size_t pos;
    while ((pos = stream->line_buffer_.find('\n')) != std::string::npos) {
        std::string line = stream->line_buffer_.substr(0, pos);
        stream->line_buffer_.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Skip heartbeat keepalives
        if (line == "Heartbeat") continue;

        // Log event lines; skip MIME boundaries and headers
        if (line.find("Code=") != std::string::npos) {
            std::cout << timestamp() << " [" << stream->config_.id
                      << "] EVENT: " << line << std::endl;
        } else if (line.find("--myboundary") == std::string::npos &&
                   line.find("Content-") == std::string::npos) {
            std::cout << timestamp() << " [" << stream->config_.id
                      << "] RAW: " << line << std::endl;
        }

        CameraEvent event;
        event.camera_id = stream->config_.id;
        if (stream->parseEventLine(line, event)) {
            stream->has_events_ = true;
            try {
                if (stream->callback_) {
                    stream->callback_(event);
                }
            } catch (const std::exception& e) {
                std::cerr << timestamp() << " [" << stream->config_.id
                          << "] ERROR in event callback: " << e.what() << std::endl;
            }
        }
    }

    return total;
}

bool AmcrestStream::parseEventLine(const std::string& line, CameraEvent& event) {
    // Format: Code=VideoMotion;action=Start;index=0
    if (line.find("Code=") == std::string::npos) return false;

    std::istringstream iss(line);
    std::string token;

    while (std::getline(iss, token, ';')) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;

        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);

        if (key == "Code") event.event_code = val;
        else if (key == "action") event.action = val;
        else if (key == "index") {
            try { event.index = std::stoi(val); } catch (...) {}
        }
    }

    return !event.event_code.empty() && !event.action.empty();
}

void AmcrestStream::streamLoop() {
    int attempt = 0;
    // Max consecutive failures before declaring camera dead
    static constexpr int MAX_FAILURES_BEFORE_ALERT = 5;

    while (running_.load()) {
        attempt++;
        std::string url = buildUrl();

        std::cout << timestamp() << " [" << config_.id
                  << "] Connection attempt #" << attempt
                  << " to " << config_.host
                  << " (consecutive failures: " << failure_count_.load() << ")"
                  << std::endl;

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << timestamp() << " [" << config_.id
                      << "] FATAL: curl_easy_init() returned null" << std::endl;
            ++failure_count_;
            goto reconnect_wait;
        }

        {
            std::string userpwd = config_.username + ":" + config_.password;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
            curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connect_timeout_);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
            // Dead stream: no bytes for 120s
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            // TCP keepalive
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
            // Verbose curl debug when many failures
            if (failure_count_.load() >= 3) {
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            }

            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                line_buffer_.clear();
            }

            last_data_time_ = std::chrono::steady_clock::now();

            CURLcode res = curl_easy_perform(curl);

            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);

            if (!running_.load()) break;

            if (res == CURLE_WRITE_ERROR) {
                // We returned 0 from writeCallback = shutdown requested
                std::cout << timestamp() << " [" << config_.id
                          << "] Stream aborted (shutdown)" << std::endl;
                break;
            }

            if (res == CURLE_OK && http_code == 200) {
                // Was connected successfully, now dropped
                std::cout << timestamp() << " [" << config_.id
                          << "] Stream closed by server after successful connection"
                          << " (HTTP " << http_code << ")" << std::endl;
                // Reset failures — it was working
                failure_count_ = 0;
                connected_ = false;
            } else {
                connected_ = false;
                int failures = ++failure_count_;

                std::cerr << timestamp() << " [" << config_.id
                          << "] STREAM FAILED: " << curl_easy_strerror(res)
                          << " (curl=" << (int)res << " HTTP=" << http_code << ")"
                          << " [failure #" << failures << "]"
                          << std::endl;

                // Detailed failure reasons
                switch (res) {
                    case CURLE_OPERATION_TIMEDOUT:
                        std::cerr << timestamp() << " [" << config_.id
                                  << "]   -> No data for 120s (camera frozen or network issue)"
                                  << std::endl;
                        break;
                    case CURLE_COULDNT_CONNECT:
                        std::cerr << timestamp() << " [" << config_.id
                                  << "]   -> Cannot reach " << config_.host
                                  << " (offline or network issue)" << std::endl;
                        break;
                    case CURLE_COULDNT_RESOLVE_HOST:
                        std::cerr << timestamp() << " [" << config_.id
                                  << "]   -> DNS resolution failed for " << config_.host
                                  << std::endl;
                        break;
                    case CURLE_RECV_ERROR:
                        std::cerr << timestamp() << " [" << config_.id
                                  << "]   -> Connection reset (firmware bug or resource limit)"
                                  << std::endl;
                        break;
                    case CURLE_GOT_NOTHING:
                        std::cerr << timestamp() << " [" << config_.id
                                  << "]   -> Server returned empty response"
                                  << std::endl;
                        break;
                    default:
                        break;
                }

                if (http_code == 401) {
                    std::cerr << timestamp() << " [" << config_.id
                              << "]   -> AUTH REJECTED (401) — wrong credentials?"
                              << std::endl;
                }

                // Check if we hit the alert threshold
                if (failures == MAX_FAILURES_BEFORE_ALERT) {
                    std::cerr << timestamp() << " [" << config_.id
                              << "] !!! ALERT THRESHOLD: " << failures
                              << " consecutive failures — publishing failure notification"
                              << std::endl;
                    gave_up_ = true;
                    // Callback will be used by NvrManager to publish MQTT alert
                    if (callback_) {
                        CameraEvent alert;
                        alert.camera_id = config_.id;
                        alert.event_code = "ConnectionFailure";
                        alert.action = "Alert";
                        alert.index = failures;
                        callback_(alert);
                    }
                }
            }
        }

        reconnect_wait:
        if (!running_.load()) break;

        // Exponential backoff: 5, 10, 20, 40, 60 (max)
        int backoff_exp = std::min(failure_count_.load() - 1, 4);
        int delay = std::min(reconnect_delay_ * (1 << std::max(backoff_exp, 0)), 60);

        std::cout << timestamp() << " [" << config_.id
                  << "] Reconnecting in " << delay << "s"
                  << " (attempt #" << (attempt + 1)
                  << ", failures: " << failure_count_.load() << ")"
                  << std::endl;

        for (int i = 0; i < delay && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // On successful reconnect after alert, clear gave_up flag
        // (this happens at the top of the next loop iteration when connected_=true)
    }

    std::cout << timestamp() << " [" << config_.id
              << "] Stream loop exited" << std::endl;
}

}  // namespace hms_nvr
