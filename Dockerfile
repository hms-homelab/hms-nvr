# HMS-NVR Dockerfile
# Multi-stage build for minimal image size
# Uses trixie (Debian 13) for library availability

# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM debian:trixie-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ca-certificates \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libyaml-cpp-dev \
    libpaho-mqttpp-dev \
    libpaho-mqtt-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /build
COPY CMakeLists.txt VERSION ./
COPY src/ src/
COPY include/ include/

# Build
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    strip hms_nvr

# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM debian:trixie-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libcurl4t64 \
    libjsoncpp25 \
    libyaml-cpp0.8 \
    libpaho-mqtt1.3 \
    libpaho-mqttpp3-1 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -r -s /bin/false hms

# Copy binary from builder
COPY --from=builder /build/build/hms_nvr /usr/local/bin/hms_nvr
RUN chmod +x /usr/local/bin/hms_nvr

# Create working directory and config directory
WORKDIR /app
RUN mkdir -p /etc/hms-nvr && chown -R hms:hms /app /etc/hms-nvr

# Copy default config
COPY config/hms-nvr.yaml.example /etc/hms-nvr/hms-nvr.yaml

# Switch to non-root user
USER hms

# Expose health check port
EXPOSE 8898

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=15s --retries=3 \
    CMD curl -f http://localhost:8898/health || exit 1

# Run the application
CMD ["/usr/local/bin/hms_nvr", "--config", "/etc/hms-nvr/hms-nvr.yaml"]
