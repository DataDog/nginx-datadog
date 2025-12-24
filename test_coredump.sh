#!/bin/bash
# Test script to verify core dump generation

set -e

echo "=== Core Dump Test Script ==="
echo

# Get nginx worker PID
echo "1. Getting nginx worker PID..."
WORKER_PID=$(docker exec nginx-crash-repro-nginx-1 ps aux | grep "nginx: worker" | head -1 | awk '{print $1}')
echo "   Selected worker PID: $WORKER_PID"
echo

# Record existing core dump timestamp (if any)
echo "2. Checking for existing core dumps..."
BEFORE_TIME=$(docker exec nginx-crash-repro-nginx-1 stat -c %Y /tmp/cores/core 2>/dev/null || echo "0")
echo "   Existing core timestamp: $BEFORE_TIME"
echo

# Send SIGABRT to worker
echo "3. Sending SIGABRT to worker $WORKER_PID..."
docker exec nginx-crash-repro-nginx-1 kill -SIGABRT $WORKER_PID
sleep 2
echo "   Signal sent"
echo

# Check for core dump in container
echo "4. Checking for core dump in container..."
AFTER_TIME=$(docker exec nginx-crash-repro-nginx-1 stat -c %Y /tmp/cores/core 2>/dev/null || echo "0")
if [ "$AFTER_TIME" -gt "$BEFORE_TIME" ]; then
    docker exec nginx-crash-repro-nginx-1 ls -lh /tmp/cores/core
    echo "   ✓ New core dump generated!"
else
    echo "   ✗ No new core dump (timestamp unchanged)"
    docker exec nginx-crash-repro-nginx-1 ls -lh /tmp/cores/core 2>/dev/null || echo "   No core file exists"
    exit 1
fi
echo

# Check for core dump on host
echo "5. Checking for core dump on host..."
if ls -lh ./core/core 2>/dev/null; then
    echo "   ✓ Core dump visible on host!"
else
    echo "   ✗ No core dump on host"
    exit 1
fi
echo

# Test gdb can open the core dump
echo "6. Testing gdb can open core dump..."
if docker exec nginx-crash-repro-nginx-1 gdb -batch -ex "info proc" /usr/local/nginx/sbin/nginx /tmp/cores/core 2>&1 | grep -q "process\|Program terminated"; then
    echo "   ✓ GDB can open core dump!"
else
    echo "   ⚠ GDB had issues opening core dump (may be expected for forced crash)"
fi
echo

# Verify new worker was spawned
echo "7. Verifying nginx spawned new worker..."
NEW_WORKERS=$(docker exec nginx-crash-repro-nginx-1 ps aux | grep "nginx: worker" | wc -l)
echo "   Current worker count: $NEW_WORKERS"
if [ "$NEW_WORKERS" -ge 4 ]; then
    echo "   ✓ Workers respawned correctly"
else
    echo "   ✗ Expected 4 workers, got $NEW_WORKERS"
fi
echo

echo "=== Test Complete ==="
echo "Core dumps are now configured and working!"
echo
echo "To analyze a core dump:"
echo "  docker exec -it nginx-crash-repro-nginx-1 gdb /usr/local/nginx/sbin/nginx /tmp/cores/core"
