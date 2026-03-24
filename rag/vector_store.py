from sentence_transformers import SentenceTransformer
import numpy as np
import faiss

model = SentenceTransformer('all-MiniLM-L6-v2')

class VectorStore:
    def __init__(self):
        self.index = faiss.IndexFlatL2(384)
        self.chunks = []

    def add(self, chunks):
        if not chunks:
            return
        embeddings = model.encode(chunks)
        self.index.add(np.array(embeddings).astype('float32'))
        self.chunks.extend(chunks)

    def search(self, query, top_k=5):
        if not self.chunks or self.index.ntotal == 0:
            return []

        k = min(top_k, len(self.chunks))
        query_embedding = model.encode([query]).astype('float32')
        distances, indices = self.index.search(query_embedding, k)

        results = []
        for i in indices[0]:
            if 0 <= i < len(self.chunks):
                results.append(self.chunks[i])
        return results