# Nginx Ingress Controller HTTP/2 Crash Reproduction Environment

This setup reproduces the nginx ingress controller environment where worker 8912 crashed with Signal 11 (SIGSEGV) while sending an HTTP/2 GOAWAY frame.

## Architecture

- **Nginx**: Extracted from `registry.k8s.io/ingress-nginx/controller:v1.14.0`
  - Based on nginx 1.27.1 with Lua, GeoIP, and other modules
  - Configured with HTTP/2 support
  - Debug logging enabled

- **Datadog Module**: Extracted from `datadog/ingress-nginx-injection:v1.14.0`
  - AppSec/WAF enabled
  - Thread pool configuration matching production

- **Configuration**: Mimics the Helm values from the crash environment
  - HTTP/2 with idle timeout (can trigger GOAWAY)
  - Proxy cache
  - Datadog AppSec enabled

## Crash Context

The crash occurred when:
1. Connection *741212 became idle
2. Idle timeout triggered
3. HTTP/2 GOAWAY frame was being sent
4. **Segmentation fault** (Signal 11)

This suggests a memory corruption issue in HTTP/2 connection cleanup, possibly:
- Use-after-free
- Double-free
- Buffer overflow in GOAWAY frame generation

## Building and Running

### 1. Build the image

```bash
cd /private/tmp/a/nginx-crash-repro
docker-compose -f docker-compose-http2.yml build
```

### 2. Run the environment

```bash
docker-compose -f docker-compose-http2.yml up
```

This starts:
- Nginx with HTTP/2 on ports 80 (redirect), 443 (HTTP/2), and 8080 (HTTP/1.1)
- Datadog agent
- Load generator that creates HTTP/2 traffic and idle connections

### 3. Monitor for crashes

Watch the nginx container logs:
```bash
./wait_for_crash.py
```

### 4. Extract core dumps (if crash occurs)

```bash
# Core dumps will be in ./core directory
ls -la ./core/

# Analyze with gdb
docker-compose -f docker-compose-http2.yml exec nginx bash
gdb /usr/bin/nginx /tmp/cores/core.XXXXX
```

## Endpoints

The backend server provides the following test endpoints:

- `/get/:bout_size/:bout_count/:wait_ms` - Streaming response with delays
- `/post/:bout_size/:bout_count/:wait_ms/:d` - POST with body reading delays
- `/json/:size_kb` - Large JSON response (>500KB for AppSec)
- `/lua/socket` - TCP socket operations
- `/lua/threads` - Light thread operations
- `/auth` - Auth header test

## Log Analysis

Compare docker logs with the original crash log:

### 1. Collect docker logs
```bash
docker-compose logs nginx > docker_log.txt
```

### 2. Build database and analyze coverage (multi-threaded Java)
```bash
./analyze.sh logs/ingress-nginx-ingress-nginx-ingress-nginx-controller-1766148689992694000.log docker_log.txt
```

### 3. Check stats later
```bash
./analyze.sh stats
```

### 4. Find missing patterns
```bash
sqlite3 log_messages.db "SELECT message, seen_on_log_count FROM log_messages WHERE seen_on_log = 1 AND seen_on_docker = 0 ORDER BY seen_on_log_count DESC LIMIT 20;"
```

Note: Analyzer uses parallel processing and typically completes in ~20 seconds

## Key Components

- **nginx**: Ingress controller with Datadog module, brotli, lua-nginx-module
- **backend**: Rust server with /get, /post, /json, /auth endpoints
- **agent**: Datadog agent (dummy config)
- **loadgen**: Continuous HTTP/2 traffic generator

## Files

- `Dockerfile.ingress` - Multi-stage build extracting nginx and Datadog module
- `nginx.conf` - HTTP/2 configuration matching production
- `entrypoint.sh` - Startup script with SSL cert generation
- `docker-compose.yml` - Full environment setup
- `analyze_crash.py` - Log analysis tool (in parent directory)

## Known Issue from Production

**Worker 8912 crash:**
- Timestamp: 2025/12/19 12:51:17
- Last action: `http2 send GOAWAY frame: last sid 1, error 0`
- Signal: 11 (SIGSEGV - Segmentation Fault)
- Context: 21 incomplete requests, idle connection timeout
