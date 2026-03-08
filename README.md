# HMS-NVR

Amcrest camera event stream monitor that publishes motion, doorbell, and connection failure events to MQTT. Designed to work with [hms-detection](https://github.com/hms-homelab/hms-detection) for YOLO-based security notifications.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-support-%23FFDD00.svg?logo=buy-me-a-coffee)](https://www.buymeacoffee.com/aamat09)
[![GHCR](https://img.shields.io/badge/ghcr.io-hms--nvr-blue?logo=docker)](https://github.com/hms-homelab/hms-nvr/pkgs/container/hms-nvr)
[![Build](https://github.com/hms-homelab/hms-nvr/actions/workflows/docker-build.yml/badge.svg)](https://github.com/hms-homelab/hms-nvr/actions)

## Features

- **Amcrest Event Streams**: Long-polls HTTP event streams with libcurl digest auth
- **Multi-Camera**: Monitor multiple Amcrest cameras with per-camera event filtering
- **MQTT Publishing**: Motion start/stop, doorbell ring, connection failure alerts
- **Motion Dedup**: Tracks per-camera motion state to suppress duplicate events
- **Connection Alerting**: Publishes `hms_nvr/alert/camera_down` after consecutive failures
- **Low Memory**: ~230 KB binary, minimal runtime footprint
- **YAML Config**: Camera credentials, events, and stream settings in one file

## Architecture

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  Amcrest Camera  │────▶│     HMS-NVR      │────▶│   MQTT Broker    │
│  Event Stream    │     │   C++ Service    │     │   (EMQX)         │
│  (HTTP Digest)   │     │   ~230 KB        │     └────────┬─────────┘
└──────────────────┘     └──────────────────┘              │
         x N cameras                              ┌────────┴─────────┐
                                                  │                  │
                                             ┌────▼─────┐    ┌──────▼──────┐
                                             │ YOLO     │    │    n8n      │
                                             │ Detection│    │  Workflows  │
                                             └──────────┘    └─────────────┘
```

## MQTT Topics

| Topic | Payload | Description |
|-------|---------|-------------|
| `camera/event/motion/start` | `{"camera_id":"patio"}` | Motion detected |
| `camera/event/motion/stop` | `{"camera_id":"patio"}` | Motion ended (configurable) |
| `camera/event/doorbell` | `{"camera_id":"front_door"}` | Doorbell ring (PhoneCallDetect) |
| `hms_nvr/alert/camera_down` | `{"camera_id":"...","failures":N}` | Camera unreachable |
| `hms_nvr/status` | `online` / `offline` | Service status (retained) |

## Quick Start

### Docker

```bash
docker run -d \
  -v ./config/hms-nvr.yaml:/etc/hms-nvr/hms-nvr.yaml:ro \
  -p 8898:8898 \
  ghcr.io/hms-homelab/hms-nvr:latest
```

Or use Docker Compose - see `docker-compose.yml`.

### Native Build

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install libjsoncpp-dev libcurl4-openssl-dev libyaml-cpp-dev \
  libpaho-mqttpp-dev libpaho-mqtt-dev libssl-dev cmake build-essential

# Build
git clone https://github.com/hms-homelab/hms-nvr.git
cd hms-nvr
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Install (systemd)

```bash
sudo cp build/hms_nvr /usr/local/bin/
sudo mkdir -p /etc/hms-nvr
sudo cp config/hms-nvr.yaml.example /etc/hms-nvr/hms-nvr.yaml
# Edit /etc/hms-nvr/hms-nvr.yaml with your camera credentials

sudo cp hms-nvr.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hms-nvr
```

## Configuration

All settings are in `hms-nvr.yaml`:

### Server

| Field | Default | Description |
|-------|---------|-------------|
| `server.port` | `8898` | Health endpoint port |
| `server.threads` | `2` | Server threads |

### MQTT

| Field | Default | Description |
|-------|---------|-------------|
| `mqtt.broker` | `192.168.2.15` | MQTT broker host |
| `mqtt.port` | `1883` | MQTT broker port |
| `mqtt.username` | - | MQTT username |
| `mqtt.password` | - | MQTT password |
| `mqtt.client_id` | `hms_nvr` | MQTT client ID |

### Cameras

```yaml
cameras:
  - id: "front_door"          # MQTT camera_id
    name: "Front Door"        # Friendly name
    host: "192.168.2.43"      # Camera IP
    username: "admin"          # Camera HTTP user
    password: "password"       # Camera HTTP password
    model: "Amcrest AD410"     # Optional, shown in health
    events:                    # Events to subscribe to
      - VideoMotion
      - PhoneCallDetect
    enabled: true
```

### Event Stream

| Field | Default | Description |
|-------|---------|-------------|
| `event_stream.reconnect_delay` | `5` | Seconds between reconnect attempts |
| `event_stream.connect_timeout` | `10` | HTTP connection timeout (seconds) |
| `event_stream.heartbeat_interval` | `30` | Stream health check interval |
| `event_stream.publish_motion_stop` | `true` | Publish stop events (set `false` for YOLO) |

## Health Endpoint

```bash
curl http://localhost:8898/health
```

```json
{
  "service": "hms-nvr",
  "version": "1.0.0",
  "cameras": [
    {
      "id": "front_door",
      "name": "Front Door",
      "model": "Amcrest AD410",
      "connected": true,
      "has_events": true,
      "running": true,
      "failures": 0
    }
  ],
  "mqtt_connected": true
}
```

## Directory Structure

```
hms-nvr/
├── src/
│   ├── main.cpp                # Entry point
│   ├── ConfigLoader.cpp        # YAML config parser
│   ├── HealthEndpoint.cpp      # HTTP health endpoint
│   ├── amcrest/
│   │   ├── AmcrestStream.cpp   # HTTP event stream long-poll
│   │   └── NvrManager.cpp      # Camera manager + MQTT publisher
│   └── mqtt/
│       └── MqttClient.cpp      # Paho MQTT wrapper
├── include/                    # Header files
├── config/
│   ├── hms-nvr.yaml.example    # Example configuration
│   └── hms-nvr.yaml            # Local config (gitignored)
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── hms-nvr.service             # Systemd service file
├── CHANGELOG.md
└── README.md
```

## Integration with hms-detection

HMS-NVR is designed to feed [hms-detection](https://github.com/hms-homelab/hms-detection) for YOLO-based object detection:

1. HMS-NVR publishes `camera/event/motion/start` with `{"camera_id":"patio"}`
2. hms-detection receives it, grabs RTSP frames, runs YOLO inference
3. On detection, publishes `yolo_detection/patio/result` with snapshot URL
4. n8n workflow downloads snapshot and sends phone notification

Set `publish_motion_stop: false` when using hms-detection, as it manages its own 30-second recording window.

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## Support

If this project is useful to you, consider buying me a coffee!

[![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/aamat09)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Related Projects

- [hms-detection](https://github.com/hms-homelab/hms-detection) - YOLO object detection for security cameras
- [hms-nut](https://github.com/hms-homelab/hms-nut) - UPS monitoring via NUT
- [hms-firetv](https://github.com/hms-homelab/hms-firetv) - Fire TV control service

Part of the [HMS Homelab](https://github.com/hms-homelab) project - lightweight C++ microservices for home automation.
