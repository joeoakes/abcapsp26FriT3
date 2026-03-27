from rag.pipeline import rag_query

answer = rag_query("Summarize the current maze run.", "TEST_MISSION")
print(answer)