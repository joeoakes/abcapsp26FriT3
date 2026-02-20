#include <microhttpd.h>
#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hiredis/hiredis.h>

#define DEFAULT_PORT 8449

redisContext *redis = NULL;
const char *redis_host;
int redis_port;
const char *key;

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

char *redis_hget_str(redisContext *c,
                     const char *key,
                     const char *field)
{
    redisReply *reply = redisCommand(c, "HGET %s %s", key, field);

    if (!reply) return NULL;

    char *out = NULL;
    if (reply->type == REDIS_REPLY_STRING) {
        out = strdup(reply->str);
    }

    freeReplyObject(reply);
    return out;
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
    (void)cls;
    (void)version;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/move") != 0)
        return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = *con_cls;

    /* Collect POST data */
    if (*upload_data_size != 0) {
        ci->data = realloc(ci->data, ci->size + *upload_data_size + 1);
        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';

        *upload_data_size = 0;
        return MHD_YES;
    }

    /* Parse JSON */
    /* Ensure POST body exists */
if (!ci->data || ci->size == 0) {
    fprintf(stderr, "Error: Empty POST body\n");

    const char *response = "{\"error\":\"empty body\"}";

    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(response),
                                        (void *)response,
                                        MHD_RESPMEM_PERSISTENT);

    int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);

    MHD_destroy_response(resp);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return ret;
}

/* Parse JSON safely */
bson_error_t error;
bson_t *doc = bson_new_from_json((uint8_t *)ci->data, -1, &error);

if (!doc) {
    fprintf(stderr, "JSON parse error: %s\n", error.message);

    const char *response = "{\"error\":\"invalid json\"}";

    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(response),
                                        (void *)response,
                                        MHD_RESPMEM_PERSISTENT);

    int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);

    MHD_destroy_response(resp);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return ret;
}

    const char *mission_id = NULL;
    const char *robot_id = NULL;
    const char *mission_type = NULL;

    int moves_left_turn = 0;
    int moves_right_turn = 0;
    int moves_straight = 0;
    int moves_reverse = 0;

    double distance_traveled = 0.0;
    long duration_seconds = 0;
    const char *mission_result = NULL;
    const char *abort_reason = NULL;

    bson_iter_t iter;

    if (bson_iter_init_find(&iter, doc, "mission_id"))
        mission_id = bson_iter_utf8(&iter, NULL);

    if (bson_iter_init_find(&iter, doc, "robot_id"))
        robot_id = bson_iter_utf8(&iter, NULL);

    if (bson_iter_init_find(&iter, doc, "mission_type"))
        mission_type = bson_iter_utf8(&iter, NULL);

    if (bson_iter_init_find(&iter, doc, "moves_left_turn"))
        moves_left_turn = bson_iter_int32(&iter);

    if (bson_iter_init_find(&iter, doc, "moves_right_turn"))
        moves_right_turn = bson_iter_int32(&iter);

    if (bson_iter_init_find(&iter, doc, "moves_straight"))
        moves_straight = bson_iter_int32(&iter);

    if (bson_iter_init_find(&iter, doc, "moves_reverse"))
        moves_reverse = bson_iter_int32(&iter);

    if (bson_iter_init_find(&iter, doc, "distance_traveled"))
        distance_traveled = bson_iter_double(&iter);

    if (bson_iter_init_find(&iter, doc, "duration_seconds"))
        duration_seconds = bson_iter_int64(&iter);

    if (bson_iter_init_find(&iter, doc, "mission_result"))
        mission_result = bson_iter_utf8(&iter, NULL);

    if (bson_iter_init_find(&iter, doc, "abort_reason"))
        abort_reason = bson_iter_utf8(&iter, NULL);

    if (!mission_id || !robot_id || !mission_type) {
        fprintf(stderr, "Missing required fields\n");

        const char *response = "{\"error\":\"missing required fields\"}";

        struct MHD_Response *resp =
            MHD_create_response_from_buffer(strlen(response),
                                            (void *)response,
                                            MHD_RESPMEM_PERSISTENT);

        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);

        MHD_destroy_response(resp);

        bson_destroy(doc);
        free(ci->data);
        free(ci);
        *con_cls = NULL;

        return ret;
    }

    if (!redis) {
        fprintf(stderr, "Redis not initialized\n");
        bson_destroy(doc);
        return MHD_NO;
    }

    time_t now = time(NULL);

    int moves_total =
        moves_left_turn +
        moves_right_turn +
        moves_straight +
        moves_reverse;

    /* Store in Redis */
    redisReply *reply = redisCommand(
        redis,
        "HSET team3fmission:%s:summary "
        "robot_id %s "
        "mission_type %s "
        "start_time %ld "
        "end_time %ld "
        "moves_left_turn %d "
        "moves_right_turn %d "
        "moves_straight %d "
        "moves_reverse %d "
        "moves_total %d "
        "distance_traveled %.2f "
        "duration_seconds %ld "
        "mission_result %s "
        "abort_reason %s",

        mission_id,
        robot_id,
        mission_type,
        now,
        now,
        moves_left_turn,
        moves_right_turn,
        moves_straight,
        moves_reverse,
        moves_total,
        distance_traveled,
        duration_seconds,
        mission_result ? mission_result : "",
        abort_reason ? abort_reason : ""
    );

    if (!reply)
    {
        fprintf(stderr, "Redis HSET failed\n");
    }
    else
    {
        printf("Stored mission %s in Redis\n", mission_id);
        freeReplyObject(reply);
    }

    bson_destroy(doc);

    /* Send HTTP response */
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
    /* Connect to Redis once */
    redis_host = getenv("redis_host");
    if (!redis_host) redis_host = "localhost";

    const char *redis_port_str;

    redis_port_str = getenv("redis_port");
    if (!redis_port_str)
        redis_port = 6379;
    else
        redis_port = atoi(redis_port_str);

    key = getenv("KEY");
    if (!key) key = "team3fmoves";

    redis = redisConnect(redis_host, redis_port);

    if (!redis || redis->err) {
        fprintf(stderr, "Redis connection error: %s\n",
                redis ? redis->errstr : "NULL");
        return 1;
    }

    const char *cert_path = "certs/server_ai.crt";
    const char *key_path  = "certs/server_ai.key";

    /* 1. LOAD CERT KEY INTO MEMORY BUFFERS */
    char *key_pem = load_file(key_path);
    char *cert_pem = load_file(cert_path);

    if (!key_pem || !cert_pem) {
        fprintf(stderr, "Error: Could not read certs.\n");
        fprintf(stderr, "Please run: mkdir -p certs && openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj '/CN=localhost'\n");
        if (redis)
            redisFree(redis);
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
        if (redis)
            redisFree(redis);
        return 1;
    }

    printf("-----------------------------------------------\n");
    printf("HTTPS server listening on https://0.0.0.0:%d\n", DEFAULT_PORT);
    printf("Database: Redis\n");
    printf("Redis Host: %s\n", redis_host);
    printf("Redis Port: %d\n", redis_port);
    printf("Key format example: team3fmission:TEST_MISSION:summary\n");
    printf("Post JSON to /move\n");
    printf("-----------------------------------------------\n");

    getchar(); 

    MHD_stop_daemon(daemon);
    
    free(key_pem);
    free(cert_pem);
    mongoc_cleanup();

    if (redis)
        redisFree(redis);

    return 0;
}
