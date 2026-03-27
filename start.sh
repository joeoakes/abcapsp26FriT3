#!/bin/bash
echo "================================================"
echo "  Mini-Pupper V2 Dashboard - Team 3F"
echo "================================================"

# Kill any existing processes
echo "[1/4] Cleaning up old processes..."
pkill -9 -f "dashboard/app" 2>/dev/null
pkill -f "ssh -L" 2>/dev/null
fuser -k 5000/tcp 2>/dev/null
fuser -k 6380/tcp 2>/dev/null
fuser -k 11434/tcp 2>/dev/null
fuser -k 27017/tcp 2>/dev/null
sleep 2

# Start tunnels - prompt for passphrase first
echo "[2/4] Opening SSH tunnels to AI server (enter passphrase when prompted)..."
ssh -L 6380:127.0.0.1:6379 \
    -L 11434:127.0.0.1:11434 \
    -L 27017:127.0.0.1:27017 \
    atp5412@10.170.8.109 -N &
TUNNEL_PID=$!
sleep 8

# Verify tunnels
echo "[3/4] Verifying connections..."
redis-cli -p 6380 ping > /dev/null 2>&1 && echo "      Redis:  OK" || echo "      Redis:  FAILED"
curl -s http://127.0.0.1:11434/ > /dev/null 2>&1 && echo "      Ollama: OK" || echo "      Ollama: FAILED"

# Start dashboard
echo "[4/4] Starting dashboard..."
echo "      Open browser: http://127.0.0.1:5000"
echo "================================================"
cd ~/abcapsp26FriT3
python3 dashboard/app.py
