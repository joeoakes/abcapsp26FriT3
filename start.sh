#!/bin/bash
echo "================================================"
echo "  Mini-Pupper V2 Dashboard - Team 3F"
echo "================================================"

# ── PSU username (edit this or set PSU_USER env var) ──
if [ -z "$PSU_USER" ]; then
    read -p "Enter your PSU ID (e.g. atp5412): " PSU_USER
fi
echo "      Using PSU ID: $PSU_USER"

# ── SSH key path (default, override with SSH_KEY env var) ──
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_ed25519}"
if [ ! -f "$SSH_KEY" ]; then
    echo "ERROR: SSH key not found at $SSH_KEY"
    echo "       Set SSH_KEY=/path/to/your/key and retry."
    exit 1
fi

# ── Kill any existing processes ──
echo "[1/4] Cleaning up old processes..."
pkill -9 -f "dashboard/app" 2>/dev/null
pkill -f "ssh -L" 2>/dev/null
sleep 1
fuser -k 5000/tcp  2>/dev/null
fuser -k 6380/tcp  2>/dev/null
fuser -k 11434/tcp 2>/dev/null
fuser -k 27017/tcp 2>/dev/null
sleep 1

# ── Add SSH key to agent ──
echo "[2/4] Adding SSH key (enter passphrase once)..."
eval $(ssh-agent -s) > /dev/null
ssh-add "$SSH_KEY"

# ── Start tunnels ──
echo "[3/4] Opening SSH tunnels to AI server (${PSU_USER}@10.170.8.109)..."
ssh -o StrictHostKeyChecking=no \
    -o ServerAliveInterval=30 \
    -o ServerAliveCountMax=3 \
    -L 6380:127.0.0.1:6379 \
    -L 11434:127.0.0.1:11434 \
    -L 27017:127.0.0.1:27017 \
    ${PSU_USER}@10.170.8.109 -N &
sleep 6

# ── Verify tunnels ──
redis-cli -p 6380 ping > /dev/null 2>&1 \
    && echo "      Redis:  OK" \
    || echo "      Redis:  FAILED (check PSU ID / VPN)"
curl -s http://127.0.0.1:11434/ > /dev/null 2>&1 \
    && echo "      Ollama: OK" \
    || echo "      Ollama: FAILED"

# ── Start dashboard ──
echo "[4/4] Starting dashboard..."
echo "      Open browser: http://127.0.0.1:5000"
echo "================================================"
cd ~/abcapsp26FriT3
python3 dashboard/app.py
