import redis

r = redis.Redis(host="localhost", port=6380, decode_responses=True)

def get_recent_logs(mission_id, limit=10):
    key = f"team3fmission:{mission_id}:summary"
    data = r.hgetall(key)

    if not data:
        return f"No mission summary found in Redis for mission {mission_id}."

    parts = [
        f"Mission {mission_id} for robot {data.get('robot_id', 'unknown')}",
        f"was a {data.get('mission_type', 'unknown')} mission",
        f"that traveled {data.get('distance_traveled', '0')} units",
        f"over {data.get('duration_seconds', '0')} seconds",
        f"and ended with result {data.get('mission_result', 'unknown')}"
    ]

    abort_reason = data.get("abort_reason", "none")
    if abort_reason and abort_reason != "none":
        parts.append(f"Abort reason: {abort_reason}")

    moves_total = data.get("moves_total", "0")
    if moves_total != "0":
        parts.append(
            f"It recorded {moves_total} total moves, including "
            f"{data.get('moves_left_turn', '0')} left turns, "
            f"{data.get('moves_right_turn', '0')} right turns, "
            f"{data.get('moves_straight', '0')} straight moves, and "
            f"{data.get('moves_reverse', '0')} reverse moves"
        )

    return ". ".join(parts) + "."