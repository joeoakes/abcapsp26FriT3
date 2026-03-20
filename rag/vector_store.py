from sentence_transformers import SentenceTransformer
import numpy as np
import faiss

model = SentenceTransformer('all-MiniLM-L6-v2')

class VectorStore:
    def __init__(self):
        self.index = faiss.IndexFlatL2(384)  # 384 is the dimension of the embeddings
        self.chunks = []

    def add(self, chunks):
        embeddings = model.encode(chunks)
        self.index.add(np.array(embeddings).astype('float32'))
        self.chunks.extend(chunks)

    def search(self, query, top_k=5):
        query_embedding = model.encode([query]).astype('float32')
        distances, indices = self.index.search(query_embedding, top_k)
        return [self.chunks[i] for i in indices[0]]
    
    