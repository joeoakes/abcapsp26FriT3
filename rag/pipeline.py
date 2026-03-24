import ollama

from rag.ingest import load_documents, chunk_text
from rag.vector_store import VectorStore
from rag.retrieve import retrieve
from rag.logs import get_recent_logs

store = VectorStore()

docs = load_documents("docs")
chunks = []

for doc in docs:
    chunks.extend(chunk_text(doc))

store.add(chunks)

def rag_query(question, mission_id):
    static_context = retrieve(store, question)
    dynamic_logs = get_recent_logs(mission_id)

    context = f"""
STATIC KNOWLEDGE:
{static_context}

RECENT TELEMETRY LOGS:
{dynamic_logs}
"""

    prompt = f"""
You are an AI assistant monitoring a robot navigating a maze.

Use the context below to answer the question.

Context:
{context}

Question:
{question}

Answer:
"""

    response = ollama.chat(
        model="llama3.2",
        messages=[{"role": "user", "content": prompt}]
    )

    return response["message"]["content"]