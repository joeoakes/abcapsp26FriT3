from rag.vector_store import VectorStore
from rag.retrieve import retrieve

store = VectorStore()

context = retrieve(store, "What telemetry does the robot send?")
print(context)