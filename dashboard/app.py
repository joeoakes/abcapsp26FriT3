import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from flask import Flask, render_template, jsonify, request, Response
import redis, ollama, urllib.request, json
client = ollama.Client(host='http://127.0.0.1:11434')
from rag.ingest import load_documents, chunk_text
from rag.vector_store import VectorStore
from rag.retrieve import retrieve

app = Flask(__name__)
r = redis.Redis(host='127.0.0.1', port=6380, decode_responses=True)

store = VectorStore()
docs_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs')
docs = load_documents(docs_path)
chunks = []
for doc in docs:
    chunks.extend(chunk_text(doc))
store.add(chunks)

CAMERA_PORT = 5001

def _proxy_stream(ip, path):
    url = f'http://{ip}:{CAMERA_PORT}{path}'
    def gen():
        try:
            req = urllib.request.urlopen(url, timeout=5)
            while True:
                chunk = req.read(4096)
                if not chunk: break
                yield chunk
        except Exception as e:
            print(f'[proxy] {url} error: {e}')
            yield b''
    return Response(gen(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/camera/stream')
def camera_stream():
    ip = request.args.get('ip', '')
    path = request.args.get('path', '/video_feed')
    if not ip: return 'missing ip', 400
    return _proxy_stream(ip, path)

@app.route('/api/camera/check')
def camera_check():
    ip = request.args.get('ip', '')
    if not ip: return jsonify({'online': False, 'error': 'no ip'})
    try:
        url = f'http://{ip}:{CAMERA_PORT}/health'
        with urllib.request.urlopen(url, timeout=2) as resp:
            data = json.loads(resp.read())
            return jsonify({'online': True, **data})
    except:
        return jsonify({'online': False})

@app.route('/api/camera/detection')
def camera_detection():
    ip = request.args.get('ip', '')
    if not ip: return jsonify({'error': 'no ip'})
    try:
        url = f'http://{ip}:{CAMERA_PORT}/detection'
        with urllib.request.urlopen(url, timeout=2) as resp:
            data = json.loads(resp.read())
            return jsonify(data)
    except:
        return jsonify({"tag_id":None,"x":None,"y":None,"z":None,"roll":None,"pitch":None,"yaw":None})

def get_mission_ids():
    try:
        keys = r.keys('team3fmission:*:summary')
        ids = []
        for k in keys:
            parts = k.split(':')
            if len(parts) == 3: ids.append(parts[1])
        return sorted(ids)
    except Exception:
        return []

def get_mission(mission_id):
    try:
        return r.hgetall(f'team3fmission:{mission_id}:summary')
    except Exception:
        return {}

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
        redis_detail = 'AI server Redis (10.170.8.109:6379)'
    except:
        redis_status = 'disconnected'
        redis_detail = 'AI server Redis unreachable'
    try:
        import pymongo
        mongo = pymongo.MongoClient('mongodb://10.170.8.130:27017/', serverSelectionTimeoutMS=2000)
        mongo.server_info()
        mongo_status = 'connected'
    except:
        mongo_status = 'disconnected'
    try:
        urllib.request.urlopen('http://127.0.0.1:11434/', timeout=2)
        ollama_status = 'connected'
    except:
        ollama_status = 'disconnected'
    return jsonify({'redis': redis_status, 'redis_detail': redis_detail,
                    'mongodb': mongo_status, 'ollama': ollama_status,
                    'robot': 'mini-pupper-v2', 'team': 'team3f'})

@app.route('/api/ask', methods=['POST'])
def api_ask():
    data = request.get_json()
    mission_id = data.get('mission_id', '')
    question = data.get('question', '')
    if not question: return jsonify({'error': 'No question provided'}), 400
    try:
        mission = get_mission(mission_id)
        context = f"""Mission ID: {mission_id}
Robot: {mission.get('robot_id', 'unknown')}
Type: {mission.get('mission_type', 'unknown')}
Result: {mission.get('mission_result', 'unknown')}
Abort Reason: {mission.get('abort_reason', 'N/A')}
Duration: {mission.get('duration_seconds', 'unknown')} seconds
Total Moves: {mission.get('moves_total', 'unknown')}
Moves Straight: {mission.get('moves_straight', 'unknown')}
Moves Left Turn: {mission.get('moves_left_turn', 'unknown')}
Moves Right Turn: {mission.get('moves_right_turn', 'unknown')}
Moves Reverse: {mission.get('moves_reverse', 'unknown')}
Distance Traveled: {mission.get('distance_traveled', 'unknown')}"""
        from rag.retrieve import retrieve as _retrieve
        rag_context = _retrieve(store, question)
        prompt = f"""You are an assistant for the Mini-Pupper V2 robotics capstone project.
Mission Data:\n{context}\nRAG Context:\n{rag_context}\nQuestion: {question}\nAnswer concisely:"""
        response = client.chat(model='llama3.2', messages=[{'role': 'user', 'content': prompt}])
        answer = response['message']['content']
        return jsonify({'answer': answer, 'mission_id': mission_id})
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
