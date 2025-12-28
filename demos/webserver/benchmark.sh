#!/bin/bash
# ============================================================================
# Quartz HTTP Server Benchmark Script
# Comprehensive benchmarking using wrk, ab, and hey
# ============================================================================

set -e

# Configuration
HOST="${HOST:-localhost}"
PORT="${PORT:-8080}"
BASE_URL="http://${HOST}:${PORT}"
DURATION="${DURATION:-30s}"
THREADS="${THREADS:-4}"
CONNECTIONS="${CONNECTIONS:-100}"

echo "=============================================="
echo "Quartz HTTP Server Benchmark Suite"
echo "=============================================="
echo ""
echo "Target: ${BASE_URL}"
echo "Duration: ${DURATION}"
echo "Threads: ${THREADS}"
echo "Connections: ${CONNECTIONS}"
echo ""

# Check which benchmarking tools are available
HAS_WRK=$(command -v wrk &> /dev/null && echo "yes" || echo "no")
HAS_AB=$(command -v ab &> /dev/null && echo "yes" || echo "no")
HAS_HEY=$(command -v hey &> /dev/null && echo "yes" || echo "no")
HAS_CURL=$(command -v curl &> /dev/null && echo "yes" || echo "no")

echo "Available tools:"
echo "  wrk: ${HAS_WRK}"
echo "  ab (Apache Bench): ${HAS_AB}"
echo "  hey: ${HAS_HEY}"
echo "  curl: ${HAS_CURL}"
echo ""

# Verify server is running
echo "Checking server health..."
if ! curl -s "${BASE_URL}/api/health" > /dev/null 2>&1; then
    echo "ERROR: Server is not responding at ${BASE_URL}"
    echo "Please start the server first:"
    echo "  ./build/quartz samples/demos/http_server.qz"
    exit 1
fi
echo "Server is healthy!"
echo ""

# Function to run wrk benchmark
run_wrk() {
    local name=$1
    local endpoint=$2
    local method=${3:-GET}
    
    echo "----------------------------------------------"
    echo "Benchmark: ${name}"
    echo "Endpoint: ${endpoint}"
    echo "Method: ${method}"
    echo "----------------------------------------------"
    
    if [ "$HAS_WRK" = "yes" ]; then
        wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} \
            --latency \
            "${BASE_URL}${endpoint}"
    else
        echo "wrk not available, using curl for basic test"
        time for i in {1..100}; do
            curl -s "${BASE_URL}${endpoint}" > /dev/null
        done
        echo "(100 sequential requests)"
    fi
    echo ""
}

# Function to run hey benchmark (Go-based)
run_hey() {
    local name=$1
    local endpoint=$2
    
    echo "----------------------------------------------"
    echo "Benchmark (hey): ${name}"
    echo "----------------------------------------------"
    
    if [ "$HAS_HEY" = "yes" ]; then
        hey -n 10000 -c ${CONNECTIONS} "${BASE_URL}${endpoint}"
    else
        echo "hey not available, skipping"
    fi
    echo ""
}

# ============================================================================
# Benchmarks
# ============================================================================

echo "=============================================="
echo "Starting Benchmarks"
echo "=============================================="
echo ""

# 1. Health Check - Minimal Response
run_wrk "Health Check (minimal response)" "/api/health"

# 2. User List - JSON Array Response  
run_wrk "User List (JSON array)" "/api/users"

# 3. Light CPU Benchmark
run_wrk "Light CPU Work (10k iterations)" "/api/benchmark"

# 4. Heavy CPU Benchmark
run_wrk "Heavy CPU Work (100k iterations)" "/api/benchmark/heavy"

# 5. Echo Request - Header parsing
run_wrk "Echo (request parsing)" "/api/echo"

# 6. Root HTML Page
run_wrk "HTML Page (root)" "/"

# ============================================================================
# Connection Scaling Test
# ============================================================================

echo "=============================================="
echo "Connection Scaling Test"
echo "=============================================="
echo ""

if [ "$HAS_WRK" = "yes" ]; then
    for conns in 10 50 100 200 500 1000; do
        echo "Connections: ${conns}"
        wrk -t4 -c${conns} -d10s "${BASE_URL}/api/health" 2>&1 | grep -E "Requests/sec|Latency"
        echo ""
    done
fi

# ============================================================================
# Summary
# ============================================================================

echo "=============================================="
echo "Server Statistics After Benchmark"
echo "=============================================="
curl -s "${BASE_URL}/api/stats" | python3 -m json.tool 2>/dev/null || curl -s "${BASE_URL}/api/stats"
echo ""

echo "=============================================="
echo "Benchmark Complete!"
echo "=============================================="
echo ""
echo "Tips for improving performance:"
echo "1. Run multiple Quartz server instances behind nginx"
echo "2. Tune kernel parameters (net.core.somaxconn, etc.)"
echo "3. Use HTTP keep-alive for connection reuse"
echo "4. Enable response compression for large payloads"
echo ""
