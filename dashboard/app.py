import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from flask import Flask, render_template, jsonify, request
import redis, ollama
from rag.ingest import load_documents, chunk_text
from rag.vector_store import VectorStore
from rag.retrieve import retrieve
from rag.logs import get_recent_logs

app = Flask(__name__)
r = redis.Redis(host='127.0.0.1', port=6379, decode_responses=True)

store = VectorStore()
docs_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs')
docs = load_documents(docs_path)
chunks = []
for doc in docs:
    chunks.extend(chunk_text(doc))
store.add(chunks)

def get_mission_ids():
    keys = r.keys('team3fmission:*:summary')
    ids = []
    for k in keys:
        parts = k.split(':')
        if len(parts) == 3:
            ids.append(parts[1])
    return sorted(ids)

def get_mission(mission_id):
    return r.hgetall(f'team3fmission:{mission_id}:summary')

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
    return jsonify({'redis': redis_status, 'robot': 'mini-pupper-v2', 'team': 'team3f'})

@app.route('/api/ask', methods=['POST'])
def api_ask():
    data = request.get_json()
    question = data.get('question', '').strip()
    mission_id = data.get('mission_id', 'TEST')
    if not question:
        return jsonify({'answer': 'Please ask a question.'})
    try:
        static_context = retrieve(store, question)
        dynamic_logs = get_recent_logs(mission_id)
        context = 'STATIC KNOWLEDGE:\n' + str(static_context) + '\n\nRECENT TELEMETRY LOGS:\n' + str(dynamic_logs)
        prompt = 'You are an AI assistant monitoring a Mini-Pupper V2 robot navigating a maze.\nUse the context below to answer the question concisely.\n\nContext:\n' + context + '\n\nQuestion:\n' + question + '\n\nAnswer:'
        response = ollama.chat(model='llama3.2', messages=[{'role': 'user', 'content': prompt}])
        return jsonify({'answer': response['message']['content']})
    except Exception as e:
        return jsonify({'answer': 'Error: ' + str(e)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)