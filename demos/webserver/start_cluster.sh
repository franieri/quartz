#!/bin/bash
# ============================================================================
# Multi-Instance Quartz Server Launcher
# Launches multiple Quartz HTTP server instances for load balancing
# ============================================================================

set -e

QUARTZ_BIN="${QUARTZ_BIN:-./build/quartz}"
SERVER_SCRIPT="${SERVER_SCRIPT:-samples/demos/http_server.qz}"
BASE_PORT="${BASE_PORT:-8080}"
NUM_INSTANCES="${NUM_INSTANCES:-4}"

echo "=============================================="
echo "Quartz Multi-Instance Server Launcher"
echo "=============================================="
echo ""
echo "Configuration:"
echo "  Quartz binary: ${QUARTZ_BIN}"
echo "  Server script: ${SERVER_SCRIPT}"
echo "  Base port: ${BASE_PORT}"
echo "  Instances: ${NUM_INSTANCES}"
echo ""

# Check if quartz binary exists
if [ ! -f "${QUARTZ_BIN}" ]; then
    echo "ERROR: Quartz binary not found at ${QUARTZ_BIN}"
    echo "Please build Quartz first: bash rebuild_core.sh"
    exit 1
fi

# Function to start a server instance
start_instance() {
    local port=$1
    local instance_id=$2
    local log_file="logs/quartz_${port}.log"
    
    mkdir -p logs
    
    echo "Starting instance ${instance_id} on port ${port}..."
    
    # Create a modified server script with the correct port
    local temp_script=$(mktemp)
    sed "s/HttpServer(\"0.0.0.0\", 8080)/HttpServer(\"0.0.0.0\", ${port})/" \
        "${SERVER_SCRIPT}" > "${temp_script}"
    
    # Start the server in background
    ${QUARTZ_BIN} "${temp_script}" > "${log_file}" 2>&1 &
    local pid=$!
    
    echo "  PID: ${pid}"
    echo "  Log: ${log_file}"
    
    # Store PID for cleanup
    echo "${pid}" >> /tmp/quartz_pids.txt
    
    # Wait a moment for server to start
    sleep 1
    
    # Check if server started successfully
    if kill -0 ${pid} 2>/dev/null; then
        echo "  Status: Running"
    else
        echo "  Status: FAILED - check ${log_file}"
        return 1
    fi
}

# Function to stop all instances
stop_all() {
    echo "Stopping all Quartz instances..."
    
    if [ -f /tmp/quartz_pids.txt ]; then
        while read pid; do
            if kill -0 ${pid} 2>/dev/null; then
                echo "  Stopping PID ${pid}..."
                kill ${pid} 2>/dev/null || true
            fi
        done < /tmp/quartz_pids.txt
        rm /tmp/quartz_pids.txt
    fi
    
    echo "All instances stopped."
}

# Handle Ctrl+C
trap stop_all INT TERM

# Parse command line arguments
case "${1:-start}" in
    start)
        # Clear any existing PIDs
        rm -f /tmp/quartz_pids.txt
        
        echo "Starting ${NUM_INSTANCES} server instances..."
        echo ""
        
        for i in $(seq 0 $((NUM_INSTANCES - 1))); do
            port=$((BASE_PORT + i))
            start_instance ${port} ${i}
        done
        
        echo ""
        echo "All instances started!"
        echo ""
        echo "Servers running on:"
        for i in $(seq 0 $((NUM_INSTANCES - 1))); do
            port=$((BASE_PORT + i))
            echo "  http://localhost:${port}"
        done
        echo ""
        echo "To test, run: curl http://localhost:${BASE_PORT}/api/health"
        echo "To stop all: $0 stop"
        echo ""
        echo "Press Ctrl+C to stop all instances..."
        
        # Wait for interrupt
        wait
        ;;
        
    stop)
        stop_all
        ;;
        
    status)
        echo "Checking instance status..."
        if [ -f /tmp/quartz_pids.txt ]; then
            while read pid; do
                if kill -0 ${pid} 2>/dev/null; then
                    echo "  PID ${pid}: Running"
                else
                    echo "  PID ${pid}: Not running"
                fi
            done < /tmp/quartz_pids.txt
        else
            echo "No instances tracked."
        fi
        ;;
        
    *)
        echo "Usage: $0 {start|stop|status}"
        exit 1
        ;;
esac
