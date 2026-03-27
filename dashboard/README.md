# Mini-Pupper V2 Web Dashboard

A Flask-based web dashboard for monitoring the Mini-Pupper V2 robot.

## Features
- Mission history from Redis
- Robot & Redis connection status
- Mission detail view (moves, distance, duration, result)
- Logs & diagnostics
- Camera feed placeholder (ready to wire up)

## Setup

Install dependencies:
```
pip3 install -r requirements.txt
```

Run the dashboard:
```
python3 app.py
```

Open in browser: http://localhost:5000

## Redis Key Format
Missions are read from:
```
team3fmission:{mission_id}:summary
```

## Team
Penn State Abington — Friday Team 3F — Spring 2026 Capstone
