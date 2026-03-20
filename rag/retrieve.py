def retrieve(store, query):

    results = store.search(query)

    context = "\n".join(results)
    
    return context