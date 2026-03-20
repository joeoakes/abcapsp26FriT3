import redis
import json

r = redis.Redis(host="localhost", port=6379, decode_responses=True)

def get_recent_logs(mission_id, limit=10):
    key = f"mission:{mission_id}:events"
    logs = r.lrange(key, -limit, -1)

    log_text = []

    for log in logs:
        try:
            data = json.loads(log)
            seq = data.get("input", {}).get("move_sequence", "unknown")
            move_dir = data.get("input", {}).get("move_dir", "unknown")
            x = data.get("player", {}).get("position", {}).get("x", "?")
            y = data.get("player", {}).get("position", {}).get("y", "?")
            goal = data.get("goal_reached", False)

            log_text.append(
                f"Move {seq}: moved {move_dir} to ({x},{y}), goal_reached={goal}"
            )
        except Exception:
            log_text.append(str(log))

    return "\n".join(log_text)