#!/bin/bash

# Apache Bench load test script
# Alternates between three endpoints indefinitely

set -e

# Configuration
HOST="https://localhost"
CONCURRENCY=${AB_CONCURRENCY:-100}
REQUESTS=${AB_REQUESTS:-50000}
TIMEOUT=${AB_TIMEOUT:-30}

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "Starting Apache Bench load test..."
echo "Host: $HOST"
echo "Concurrency: $CONCURRENCY"
echo "Requests per test: $REQUESTS"
echo "Press Ctrl+C to stop"
echo ""

# Counter for iterations
iteration=1

while true; do
    echo -e "${BLUE}=== Iteration $iteration ===${NC}"

    # Test 1: GET /get/10/5/500
    echo -e "${GREEN}[$(date +%H:%M:%S)] Testing GET /get/10/5/500${NC}"
    ab -n $REQUESTS -c $CONCURRENCY -k -s 30 "$HOST/get/10/5/500" 2>&1 || true
    echo ""

    # Test 2: POST /post/10/5/500/500
    echo -e "${GREEN}[$(date +%H:%M:%S)] Testing POST /post/10/5/500/500${NC}"
    ab -n $REQUESTS -c $CONCURRENCY -k -s 30 -p /dev/stdin "$HOST/post/10/5/500/500" <<< "test data payload" 2>&1 || true
    echo ""

    # Test 3: GET /json/600
    echo -e "${GREEN}[$(date +%H:%M:%S)] Testing GET /json/600${NC}"
    ab -n $REQUESTS -c $CONCURRENCY -k -s 30 "$HOST/json/600" 2>&1 || true
    echo ""

    echo -e "${YELLOW}Completed iteration $iteration${NC}"
    echo ""

    ((iteration++))

    # Small delay between iterations
    sleep 2
done
