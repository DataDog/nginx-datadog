#!/bin/bash
set -e

# Generate self-signed certificate for HTTP/2 testing
mkdir -p /etc/nginx/ssl
if [ ! -f /etc/nginx/ssl/nginx.crt ]; then
    echo "Generating self-signed certificate for HTTP/2..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
        -keyout /etc/nginx/ssl/nginx.key \
        -out /etc/nginx/ssl/nginx.crt \
        -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost"
fi

# Create cache directory
mkdir -p /var/cache/nginx/common_proxy_cache
chown -R www-data:www-data /var/cache/nginx 2>/dev/null || true

# Set up core dump directory with proper permissions
mkdir -p /tmp/cores
chown www-data:www-data /tmp/cores
chmod 755 /tmp/cores

# Wait for backend_rs to be ready
echo "Waiting for backend_rs to be ready..."
for i in {1..30}; do
    if curl -s http://backend:3000/get/1/1/0 >/dev/null 2>&1; then
        echo "Backend is ready!"
        break
    fi
    echo "Waiting for backend... ($i/30)"
    sleep 1
done

# Test nginx configuration
echo "Testing nginx configuration..."
nginx -t

# Start nginx in foreground (daemon off already in config)
echo "Starting nginx with HTTP/2 support and Datadog AppSec..."
exec nginx
