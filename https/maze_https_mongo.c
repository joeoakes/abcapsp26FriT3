#include <microhttpd.h>
#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 8449

const char *uri_str;
const char *db_name;
const char *col_name;

/* --- Helper to load file content into memory (REQUIRED for MHD SSL) --- */
char *load_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, f);
        buffer[length] = '\0';
    }
    fclose(f);
    return buffer;
}

struct connection_info {
    char *data;
    size_t size;
};

static void get_utc_iso8601(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static int handle_post(void *cls,
                       struct MHD_Connection *connection,
                       const char *url,
                       const char *method,
                       const char *version,
                       const char *upload_data,
                       size_t *upload_data_size,
                       void **con_cls)
{
    (void)version;
    (void)cls;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/move") != 0)
        return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = *con_cls;

    if (*upload_data_size != 0) {
        ci->data = realloc(ci->data, ci->size + *upload_data_size + 1);
        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* MongoDB insert */
    bson_error_t error;
    bson_t *doc = bson_new_from_json((uint8_t *)ci->data, -1, &error);
    if (!doc) {
        fprintf(stderr, "JSON error: %s\n", error.message);
        return MHD_NO;
    }

    char ts[64];
    get_utc_iso8601(ts, sizeof(ts));
    BSON_APPEND_UTF8(doc, "received_at", ts);

    uri_str = getenv("MONGO_URI");
    if (!uri_str) uri_str = "mongodb://localhost:27017";

    mongoc_client_t *client = mongoc_client_new(uri_str);
    
    db_name = getenv("MONGO_DB");
    if (!db_name) db_name = "maze";
    
    col_name = getenv("MONGO_COL");
    if (!col_name) col_name = "team3fmoves";

    mongoc_collection_t *col = mongoc_client_get_collection(client, db_name, col_name);

    if (!mongoc_collection_insert_one(col, doc, NULL, NULL, &error)) {
    fprintf(stderr, "Mongo insert failed: %s\n", error.message);
}

    //mongoc_collection_insert_one(col, doc, NULL, NULL, &error);

    mongoc_collection_destroy(col);
    mongoc_client_destroy(client);
    bson_destroy(doc);

    const char *response = "{\"status\":\"ok\"}";
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(response),
                                         (void *)response,
                                         MHD_RESPMEM_PERSISTENT);

    int ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return ret;
}

int main(void) {
    const char *cert_path = "certs/server.crt";
    const char *key_path  = "certs/server.key";

    /* 1. LOAD CERT KEY INTO MEMORY BUFFERS */
    char *key_pem = load_file(key_path);
    char *cert_pem = load_file(cert_path);

    if (!key_pem || !cert_pem) {
        fprintf(stderr, "Error: Could not read certs.\n");
        fprintf(stderr, "Please run: mkdir -p certs && openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj '/CN=localhost'\n");
        return 1;
    }

    mongoc_init();

    /* 2. Pass MEMORY BUFFERS (cert_pem, key_pem) to the daemon */
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS | MHD_USE_INTERNAL_POLLING_THREAD,
        DEFAULT_PORT,
        NULL, NULL,
        &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
        MHD_OPTION_HTTPS_MEM_KEY, key_pem,
        MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "Failed to start HTTPS server. Check port %d\n", DEFAULT_PORT);
        return 1;
    }

    uri_str = getenv("MONGO_URI");
    if (!uri_str) uri_str = "mongodb://localhost:27017";

    db_name = getenv("MONGO_DB");
    if (!db_name) db_name = "maze";

    col_name = getenv("MONGO_COL");
    if (!col_name) col_name = "team3fmoves";

    printf("----------------------------------------------------------------------\n");
    printf("HTTPS server listening on https://0.0.0.0:%d\n", DEFAULT_PORT);
    printf("Database: MongoDB\n");
    printf("MongoDB URI: %s\n", uri_str);
    printf("Database: %s\n", db_name);
    printf("Collection: %s\n", col_name);
    printf("Post JSON to /move\n");
    printf("----------------------------------------------------------------------\n");
    getchar(); 

    MHD_stop_daemon(daemon);
    
    free(key_pem);
    free(cert_pem);
    mongoc_cleanup();

    return 0;
}
