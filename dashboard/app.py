from flask import Flask, render_template, jsonify
import redis
import json

app = Flask(__name__)
r = redis.Redis(host='127.0.0.1', port=6379, decode_responses=True)

def get_mission_ids():
    keys = r.keys('team3fmission:*:summary')
    ids = []
    for k in keys:
        parts = k.split(':')
        if len(parts) == 3:
            ids.append(parts[1])
    return sorted(ids)

def get_mission(mission_id):
    key = f'team3fmission:{mission_id}:summary'
    return r.hgetall(key)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/missions')
def api_missions():
    ids = get_mission_ids()
    missions = []
    for mid in ids:
        m = get_mission(mid)
        m['id'] = mid
        missions.append(m)
    return jsonify(missions)

@app.route('/api/mission/<mission_id>')
def api_mission(mission_id):
    m = get_mission(mission_id)
    m['id'] = mission_id
    return jsonify(m)

@app.route('/api/status')
def api_status():
    try:
        r.ping()
        redis_status = 'connected'
    except:
        redis_status = 'disconnected'
    return jsonify({
        'redis': redis_status,
        'robot': 'mini-pupper-v2',
        'team': 'team3f'
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
