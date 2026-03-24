def retrieve(store, query):
    results = store.search(query)
    if not results:
        return "No matching static documents were found."
    return "\n".join(results)