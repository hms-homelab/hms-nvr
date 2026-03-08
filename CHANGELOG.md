# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2026-03-08

### Added
- Amcrest camera HTTP event stream long-polling with libcurl digest auth
- Multi-camera support with per-camera event filtering
- MQTT publishing for motion start/stop, doorbell, and connection failure events
- Configurable `publish_motion_stop` flag (for YOLO recording window compatibility)
- Connection failure alerting after consecutive failures (`hms_nvr/alert/camera_down`)
- Health endpoint with per-camera status (connected, has_events, failure count)
- Automatic reconnection with configurable delay
- YAML configuration with camera credentials and event stream settings
- Systemd service file with TimeoutStopSec for graceful curl cleanup
- Docker multi-stage build (Debian Trixie) with GHCR publishing
- Multi-arch support (amd64 + arm64)
