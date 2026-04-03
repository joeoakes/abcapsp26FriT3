// maze_sdl2.c
// Simple SDL2 maze: generate (DFS backtracker), draw, move player to goal.
// Controls: Arrow keys or WASD. R = regenerate. Esc = quit.
// Compile: gcc maze_sdl2.c -o maze_sdl2 `sdl2-config --cflags --libs` -lcurl $(pkg-config --cflags --libs libmongoc-1.0)

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include <bson/bson.h>
#include <hiredis/hiredis.h>
#include <unistd.h>
#include <hiredis/hiredis.h>
#include "maze_learning.h"

#define MAZE_W 21   // number of cells horizontally
#define MAZE_H 15   // number of cells vertically
#define CELL   32   // pixels per cell
#define PAD    16   // window padding around maze
#define URL_ENDPOINT_LOGGING "https://10.170.8.130:8449/move"
#define URL_ENDPOINT_MP "https://10.170.9.61:8449/move"
#define JSON_BUFFER_SIZE 4096
CURL *curl;


// Wall bitmask for each cell
enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
  uint8_t walls;
  bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

LearnedMaze learned;
redisContext *learning_redis = NULL;
const char *maze_id = "default_maze_21x15";
int last_ai_px = -1;
int last_ai_py = -1;




#include "astar.h"

static inline bool in_bounds(int x, int y) {
  return (x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H);
}

// Remove wall between (x,y) and (nx,ny)
static void knock_down(int x, int y, int nx, int ny) {
  if (nx == x && ny == y - 1) { // N
    g[y][x].walls &= ~WALL_N;
    g[ny][nx].walls &= ~WALL_S;
  } else if (nx == x + 1 && ny == y) { // E
    g[y][x].walls &= ~WALL_E;
    g[ny][nx].walls &= ~WALL_W;
  } else if (nx == x && ny == y + 1) { // S
    g[y][x].walls &= ~WALL_S;
    g[ny][nx].walls &= ~WALL_N;
  } else if (nx == x - 1 && ny == y) { // W
    g[y][x].walls &= ~WALL_W;
    g[ny][nx].walls &= ~WALL_E;
  }
}

static void maze_init(void) {
  for (int y = 0; y < MAZE_H; y++) {
    for (int x = 0; x < MAZE_W; x++) {
      g[y][x].walls = WALL_N | WALL_E | WALL_S | WALL_W;
      g[y][x].visited = false;
    }
  }
}

// Iterative DFS "recursive backtracker"
static void maze_generate(int sx, int sy) {
  typedef struct { int x, y; } P;
  P stack[MAZE_W * MAZE_H];
  int top = 0;

  g[sy][sx].visited = true;
  stack[top++] = (P){sx, sy};

  while (top > 0) {
    P cur = stack[top - 1];
    int x = cur.x, y = cur.y;

    // Collect unvisited neighbors
    P neigh[4];
    int ncount = 0;

    const int dx[4] = { 0, 1, 0, -1 };
    const int dy[4] = { -1, 0, 1, 0 };

    for (int i = 0; i < 4; i++) {
      int nx = x + dx[i], ny = y + dy[i];
      if (in_bounds(nx, ny) && !g[ny][nx].visited) {
        neigh[ncount++] = (P){nx, ny};
      }
    }

    if (ncount == 0) {
      // backtrack
      top--;
      continue;
    }

    // choose random neighbor
    int pick = rand() % ncount;
    int nx = neigh[pick].x, ny = neigh[pick].y;

    // carve passage
    knock_down(x, y, nx, ny);
    g[ny][nx].visited = true;
    stack[top++] = (P){nx, ny};
  }

  // Clear visited flags so we can reuse for other logic later if needed
  for (int y = 0; y < MAZE_H; y++)
    for (int x = 0; x < MAZE_W; x++)
      g[y][x].visited = false;
}

// Draw maze walls as lines
static void draw_maze(SDL_Renderer* r) {
  // Background
  SDL_SetRenderDrawColor(r, 15, 15, 18, 255);
  SDL_RenderClear(r);

  // Maze lines
  SDL_SetRenderDrawColor(r, 230, 230, 230, 255);

  int ox = PAD;
  int oy = PAD;

  for (int y = 0; y < MAZE_H; y++) {
    for (int x = 0; x < MAZE_W; x++) {
      int x0 = ox + x * CELL;
      int y0 = oy + y * CELL;
      int x1 = x0 + CELL;
      int y1 = y0 + CELL;

      uint8_t w = g[y][x].walls;

      if (w & WALL_N) SDL_RenderDrawLine(r, x0, y0, x1, y0);
      if (w & WALL_E) SDL_RenderDrawLine(r, x1, y0, x1, y1);
      if (w & WALL_S) SDL_RenderDrawLine(r, x0, y1, x1, y1);
      if (w & WALL_W) SDL_RenderDrawLine(r, x0, y0, x0, y1);
    }
  }
}

// Player / goal rendering
static void draw_player_goal(SDL_Renderer* r, int px, int py) {
  int ox = PAD;
  int oy = PAD;

  // Goal cell highlight
  SDL_Rect goal = {
    ox + (MAZE_W - 1) * CELL + 6,
    oy + (MAZE_H - 1) * CELL + 6,
    CELL - 12,
    CELL - 12
  };
  SDL_SetRenderDrawColor(r, 40, 160, 70, 255);
  SDL_RenderFillRect(r, &goal);

  // Player
  SDL_Rect p = {
    ox + px * CELL + 8,
    oy + py * CELL + 8,
    CELL - 16,
    CELL - 16
  };
  SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
  SDL_RenderFillRect(r, &p);
}

// Attempt to move player; returns true if moved
static bool try_move(int* px, int* py, int dx, int dy) {
  int x = *px, y = *py;
  int nx = x + dx, ny = y + dy;
  if (!in_bounds(nx, ny)) return false;

  uint8_t w = g[y][x].walls;

  // Blocked by wall?
  if (dx == 0 && dy == -1 && (w & WALL_N)) return false;
  if (dx == 1 && dy == 0  && (w & WALL_E)) return false;
  if (dx == 0 && dy == 1  && (w & WALL_S)) return false;
  if (dx == -1 && dy == 0 && (w & WALL_W)) return false;

  *px = nx;
  *py = ny;

  learning_mark_open(&learned, *px, *py);
  learning_mark_visit(&learned, *px, *py);

  return true;
}

static bool can_move_from(int px, int py, int dx, int dy) {
    int nx = px + dx, ny = py + dy;
    if (!in_bounds(nx, ny)) return false;

    uint8_t w = g[py][px].walls;

    if (dx == 0 && dy == -1 && (w & WALL_N)) return false;
    if (dx == 1 && dy == 0  && (w & WALL_E)) return false;
    if (dx == 0 && dy == 1  && (w & WALL_S)) return false;
    if (dx == -1 && dy == 0 && (w & WALL_W)) return false;

    return true;
}

static bool do_learning_ai_step(int *px, int *py, char *orientation,
                                int *moves_left_turn, int *moves_right_turn,
                                int *moves_straight, int *moves_reverse,
                                const char **last_turn) {
    typedef struct {
        char move;
        int nx, ny;
        int visits;
        bool is_backtrack;
    } Option;

    Option opts[4];
    int count = 0;

    int dxs[4] = {0, 1, 0, -1};
    int dys[4] = {-1, 0, 1, 0};
    char mvs[4] = {'N', 'E', 'S', 'W'};

    for (int i = 0; i < 4; i++) {
        int dx = dxs[i], dy = dys[i];
        int nx = *px + dx, ny = *py + dy;

        if (!can_move_from(*px, *py, dx, dy)) continue;

        opts[count].move = mvs[i];
        opts[count].nx = nx;
        opts[count].ny = ny;
        opts[count].visits = learned.visit_count[ny][nx];
        opts[count].is_backtrack = (nx == last_ai_px && ny == last_ai_py);
        count++;
    }

    if (count == 0) {
        printf("AI has no move from (%d,%d)\n", *px, *py);
        return false;
    }

    int best = -1;

    for (int i = 0; i < count; i++) {
        if (opts[i].is_backtrack) continue;

        if (best == -1 || opts[i].visits < opts[best].visits) {
            best = i;
        }
    }

    if (best == -1) {
        for (int i = 0; i < count; i++) {
            if (best == -1 || opts[i].visits < opts[best].visits) {
                best = i;
            }
        }
    }

    char move = opts[best].move;
    bool hasMoved = false;
    int oldx = *px, oldy = *py;

    if (move == 'N') {
        hasMoved = try_move(px, py, 0, -1);
        if (hasMoved) {
            if (*orientation == 'N') { (*moves_straight)++; *last_turn = "forward"; }
            else if (*orientation == 'E') { (*moves_left_turn)++; *last_turn = "left"; }
            else if (*orientation == 'S') { (*moves_reverse)++; *last_turn = "backward"; }
            else if (*orientation == 'W') { (*moves_right_turn)++; *last_turn = "right"; }
            *orientation = 'N';
        }
    } else if (move == 'E') {
        hasMoved = try_move(px, py, 1, 0);
        if (hasMoved) {
            if (*orientation == 'N') { (*moves_right_turn)++; *last_turn = "right"; }
            else if (*orientation == 'E') { (*moves_straight)++; *last_turn = "forward"; }
            else if (*orientation == 'S') { (*moves_left_turn)++; *last_turn = "left"; }
            else if (*orientation == 'W') { (*moves_reverse)++; *last_turn = "backward"; }
            *orientation = 'E';
        }
    } else if (move == 'S') {
        hasMoved = try_move(px, py, 0, 1);
        if (hasMoved) {
            if (*orientation == 'N') { (*moves_reverse)++; *last_turn = "backward"; }
            else if (*orientation == 'E') { (*moves_right_turn)++; *last_turn = "right"; }
            else if (*orientation == 'S') { (*moves_straight)++; *last_turn = "forward"; }
            else if (*orientation == 'W') { (*moves_left_turn)++; *last_turn = "left"; }
            *orientation = 'S';
        }
    } else if (move == 'W') {
        hasMoved = try_move(px, py, -1, 0);
        if (hasMoved) {
            if (*orientation == 'N') { (*moves_left_turn)++; *last_turn = "left"; }
            else if (*orientation == 'E') { (*moves_reverse)++; *last_turn = "backward"; }
            else if (*orientation == 'S') { (*moves_right_turn)++; *last_turn = "right"; }
            else if (*orientation == 'W') { (*moves_straight)++; *last_turn = "forward"; }
            *orientation = 'W';
        }
    }

    if (hasMoved) {
        last_ai_px = oldx;
        last_ai_py = oldy;
        printf("AI pos: (%d,%d)\n", *px, *py);
        return true;
    }

    printf("AI has no move from (%d,%d)\n", *px, *py);
    return false;
}

static void regenerate(int* px, int* py, SDL_Window* win) {
  maze_init();
  maze_generate(0, 0);
  *px = 0; *py = 0;
  last_ai_px = -1;
  last_ai_py = -1;

  learning_mark_open(&learned, *px, *py);
  learning_mark_visit(&learned, *px, *py);
  learning_mark_goal(&learned, MAZE_W - 1, MAZE_H - 1);
  SDL_SetWindowTitle(win, "SDL2 Maze - Reach the green goal (R to regenerate)");
}

int post_json_to_move(const char* url, const char* json_data) {
    CURLcode res;
    int success = 0;

    curl_easy_reset(curl);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    // Enable TLS
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

    // --- Self-signed certificate handling ---
    // 1️⃣ Point curl to your server cert
    curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/ca.crt");
    // 2️⃣ Keep verification enabled (recommended)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // client certificate (for mTLS)
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "certs/client.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY,  "certs/client.key");

    // Timeout settings
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500);  // 500ms to connect
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1000);        // 1000ms total

    // Optional: verbose output for debugging TLS handshake
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        success = 1;
    }

    curl_slist_free_all(headers);
    return success;
}


// Function to create the JSON string
void create_player_move_json(
    const char *event_type,
    const char *device,
    int move_sequence,
    int pos_x,
    int pos_y,
    bool goal_reached,
    const char *timestamp,
    const char *move_dir,
    char *output_buffer, // pass in a pre-allocated buffer
    size_t buffer_size
) {
    snprintf(
        output_buffer, buffer_size,
        "{"
            "\"event_type\": \"%s\","
            "\"input\": {\"device\": \"%s\", \"move_sequence\": %d, \"move_dir\": \"%s\"},"
            "\"player\": {\"position\": {\"x\": %d, \"y\": %d}},"
            "\"goal_reached\": %s,"
            "\"timestamp\": \"%s\""
        "}",
        event_type,
        device,
        move_sequence,
        move_dir,
        pos_x,
        pos_y,
        goal_reached ? "true" : "false",
        timestamp
    );
}

char* create_mission_summary_json(
    const char* mission_id,
    const char* robot_id,
    const char* mission_type,
    long start_time,
    long end_time,
    int moves_left_turn,
    int moves_right_turn,
    int moves_straight,
    int moves_reverse,
    int moves_total,
    double distance_traveled,
    long duration_seconds,
    const char* mission_result,
    const char* abort_reason)
{
    char* json = malloc(4096);
    if (!json) return NULL;

    snprintf(json, 4096,
        "{"
        "\"mission_id\":\"%s\","
        "\"robot_id\":\"%s\","
        "\"mission_type\":\"%s\","
        "\"start_time\":%ld,"
        "\"end_time\":%ld,"
        "\"moves_left_turn\":%d,"
        "\"moves_right_turn\":%d,"
        "\"moves_straight\":%d,"
        "\"moves_reverse\":%d,"
        "\"moves_total\":%d,"
        "\"distance_traveled\":%.2f,"
        "\"duration_seconds\":%ld,"
        "\"mission_result\":\"%s\","
        "\"abort_reason\":\"%s\""
        "}",
        mission_id,
        robot_id,
        mission_type,
        start_time,
        end_time,
        moves_left_turn,
        moves_right_turn,
        moves_straight,
        moves_reverse,
        moves_total,
        distance_traveled,
        duration_seconds,
        mission_result,
        abort_reason
    );

    return json;
}

void print_pretty_json(const char *json_str) {
    if (!json_str || !*json_str) {
        printf("{}\n");
        return;
    }

    bson_error_t err;
    bson_t *doc = bson_new_from_json((const uint8_t*)json_str, -1, &err);
    if (!doc) {
        fprintf(stderr, "Invalid JSON: %s\n", err.message);
        return;
    }

    // Convert to pretty-printed JSON
    char *pretty = bson_as_canonical_extended_json(doc, NULL);
    printf("%s\n", pretty);

    bson_free(pretty);
    bson_destroy(doc);
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, len, f);
    buf[len] = '\0';

    fclose(f);
    return buf;
}

int load_maze_data(const char *path,
                   char *mission_id,
                   size_t mission_id_sz,
                   int *px,
                   int *py,
                   char *dir)
{
    if (!path || !mission_id || !px || !py || !dir) return 0;

    // Read entire file
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open file '%s'\n", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *json = malloc(len + 1);
    if (!json) {
        fclose(f);
        return 0;
    }

    if (fread(json, 1, len, f) != (size_t)len) {
        fprintf(stderr, "Failed to read file '%s'\n", path);
        free(json);
        fclose(f);
        return 0;
    }
    json[len] = '\0';
    fclose(f);

    // Parse JSON using BSON
    bson_error_t err;
    bson_t *doc = bson_new_from_json((const uint8_t*)json, -1, &err);
    free(json);

    if (!doc) {
        fprintf(stderr, "Failed to parse JSON: %s\n", err.message);
        return 0;
    }

    bson_iter_t iter;

    // mission_id
    if (bson_iter_init_find(&iter, doc, "mission_id") && BSON_ITER_HOLDS_UTF8(&iter)) {
        strncpy(mission_id, bson_iter_utf8(&iter, NULL), mission_id_sz - 1);
        mission_id[mission_id_sz - 1] = '\0';
    } else {
        fprintf(stderr, "Missing or invalid mission_id\n");
        bson_destroy(doc);
        return 0;
    }

    // px
    if (bson_iter_init_find(&iter, doc, "px") && BSON_ITER_HOLDS_INT32(&iter)) {
        *px = bson_iter_int32(&iter);
        if (*px < 0 || *px >= MAZE_W) {
            fprintf(stderr, "Invalid px value %d\n", *px);
            bson_destroy(doc);
            return 0;
        }
    } else {
        fprintf(stderr, "Missing or invalid px\n");
        bson_destroy(doc);
        return 0;
    }

    // py
    if (bson_iter_init_find(&iter, doc, "py") && BSON_ITER_HOLDS_INT32(&iter)) {
        *py = bson_iter_int32(&iter);
        if (*py < 0 || *py >= MAZE_H) {
            fprintf(stderr, "Invalid py value %d\n", *py);
            bson_destroy(doc);
            return 0;
        }
    } else {
        fprintf(stderr, "Missing or invalid py\n");
        bson_destroy(doc);
        return 0;
    }

    // dir
    if (bson_iter_init_find(&iter, doc, "dir") && BSON_ITER_HOLDS_UTF8(&iter)) {
        const char *s = bson_iter_utf8(&iter, NULL);
        if (s[0] == 'N' || s[0] == 'S' || s[0] == 'E' || s[0] == 'W') {
            *dir = s[0];
        } else {
            fprintf(stderr, "Invalid dir value '%c', defaulting to 'E'\n", s[0]);
            *dir = 'E';
        }
    } else {
        fprintf(stderr, "Missing dir, defaulting to 'E'\n");
        *dir = 'E';
    }

    bson_destroy(doc);
    return 1;
}

int save_maze_data(const char *path,
                   const char *mission_id,
                   int px,
                   int py,
                   char dir)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;

    fprintf(f,
        "{\n"
        "  \"mission_id\": \"%s\",\n"
        "  \"px\": %d,\n"
        "  \"py\": %d,\n"
        "  \"dir\": \"%c\"\n"
        "}\n",
        mission_id, px, py, dir);

    fflush(f);
    fclose(f);
    return 1;
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

int save_maze_grid(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            uint8_t w = g[y][x].walls;
            fwrite(&w, 1, 1, f);
        }
    }

    fclose(f);
    return 1;
}

int load_maze_grid(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            uint8_t w;
            if (fread(&w, 1, 1, f) != 1) {
                fclose(f);
                return 0;
            }
            g[y][x].walls = w;
        }
    }

    fclose(f);
    return 1;
}

int main(int argc, char** argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();
  if (!curl) {
      fprintf(stderr, "Failed to initialize curl\n");
      curl_global_cleanup();
      return 0;
  }
  char *REDIS_HOST = "127.0.0.1";
  int REDIS_PORT = 6379;
  redisContext *c = redisConnect(REDIS_HOST, REDIS_PORT);
  char mission_id[128] = "TEST_MISSION";  // default
  char robot_id[128] = "TEST_ROBOT";      // default
  char mission_type[128] = "patrol";      // default

  if (c == NULL || c->err) {
      if (c) printf("Redis error: %s\n", REDIS_HOST);
      else printf("Can't allocate redis context\n");
      return 1;

  }
  learning_init(&learned);
  learning_redis = c;
  if (learning_redis && !learning_redis->err) {
      learning_load(learning_redis, maze_id, &learned);

      printf("Learning load: run=%d known=%d reused=%d\n",
       learned.run_number,
       learned.known_cells,
       learned.reused_prior_knowledge);
  }


  

  
  printf("-----------------------------------------------\n");
  printf("Database: Redis\n");
  printf("Redis Host: %s\n", REDIS_HOST);
  printf("Redis Port: %d\n", REDIS_PORT);
  printf("Key: team3fmission:%s:summary\n", mission_id);
  printf("-----------------------------------------------\n");

int tombstone_mode = 0;

for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--tombstone") == 0) {
        tombstone_mode = 1;
    }
}
  time_t start_time = time(NULL);
  time_t end_time = time(NULL);
  int moves_left_turn = 0;
  int moves_right_turn = 0;
  int moves_straight = 0;
  int moves_reverse = 0;
  double distance_traveled = 0.0;

  

  (void)argc; (void)argv;
  srand((unsigned)time(NULL));
  char json[JSON_BUFFER_SIZE];
  int moveSequence = 0;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  int win_w = PAD * 2 + MAZE_W * CELL;
  int win_h = PAD * 2 + MAZE_H * CELL;

  SDL_Window* win = SDL_CreateWindow(
    "SDL2 Maze - Reach the green goal (R to regenerate)",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    win_w, win_h,
    SDL_WINDOW_SHOWN
  );
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!r) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  int px = 0, py = 0;
  regenerate(&px, &py, win);

  char orientation = 'E';

  bool running = true;
  bool won = false;

  if (!tombstone_mode)
  {
    redisReply *reply = redisCommand(
    c,
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
    robot_id,                  // e.g. "mini_01"
    mission_type,              // e.g. "exploration"
    start_time,                // time_t or 0
    end_time,                  // time_t or 0
    moves_left_turn,                         // moves_left_turn
    moves_right_turn,                         // moves_right_turn
    moves_straight,                         // moves_straight
    moves_reverse,                         // moves_reverse
    0,                         // moves_total
    0.0,                       // distance_traveled
    0L,                        // duration_seconds
    "aborted",                 // mission_result
    "user exited"              // abort_reason
    );
    freeReplyObject(reply);
    save_maze_grid("maze_grid.dat");
  }
  else 
  {
    if (!load_maze_grid("maze_grid.dat")) {
        fprintf(stderr, "Failed to load saved maze, generating new\n");
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    
    if (!load_maze_data("maze_data.json",
                        mission_id, sizeof(mission_id),
                        &px, &py, &orientation)) {
        fprintf(stderr, "Failed to load maze_data.json\n");
        exit(1);
    }



    char key[128];
    snprintf(key, sizeof(key), "team3fmission:%s:summary", mission_id);

    char *tmp;

    // robot_id
    tmp = redis_hget_str(c, key, "robot_id");
    if (tmp) {
        strncpy(robot_id, tmp, sizeof(robot_id)-1);
        robot_id[sizeof(robot_id)-1] = '\0';
        free(tmp);
    } else {
        strncpy(robot_id, "TEST_ROBOT", sizeof(robot_id)-1);
        robot_id[sizeof(robot_id)-1] = '\0';
    }

    // mission_type
    tmp = redis_hget_str(c, key, "mission_type");
    if (tmp) {
        strncpy(mission_type, tmp, sizeof(mission_type)-1);
        mission_type[sizeof(mission_type)-1] = '\0';
        free(tmp);
    } else {
        strncpy(mission_type, "patrol", sizeof(mission_type)-1);
        mission_type[sizeof(mission_type)-1] = '\0';
    }

    // start_time
    tmp = redis_hget_str(c, key, "start_time");
    start_time = tmp ? atol(tmp) : time(NULL);
    if (tmp) free(tmp);

    // end_time
    tmp = redis_hget_str(c, key, "end_time");
    end_time = tmp ? atol(tmp) : start_time;
    if (tmp) free(tmp);

    // moves_left_turn
    tmp = redis_hget_str(c, key, "moves_left_turn");
    moves_left_turn = tmp ? atoi(tmp) : 0;
    if (tmp) free(tmp);

    // moves_right_turn
    tmp = redis_hget_str(c, key, "moves_right_turn");
    moves_right_turn = tmp ? atoi(tmp) : 0;
    if (tmp) free(tmp);

    // moves_straight
    tmp = redis_hget_str(c, key, "moves_straight");
    moves_straight = tmp ? atoi(tmp) : 0;
    if (tmp) free(tmp);

    // moves_reverse
    tmp = redis_hget_str(c, key, "moves_reverse");
    moves_reverse = tmp ? atoi(tmp) : 0;
    if (tmp) free(tmp);

    printf("Loaded tombstone: mission='%s', robot='%s', type='%s', px=%d, py=%d, dir=%c\n",
           mission_id, robot_id, mission_type, px, py, orientation);
  }

  if (px == MAZE_W - 1 && py == MAZE_H - 1) {
    won = true;
    SDL_SetWindowTitle(win, "You win! Press R to regenerate, Esc to quit");
  }
  while (running) {
    SDL_Event e;
    bool hasMoved;
    while (SDL_PollEvent(&e)) {
      hasMoved = false;

      if (e.type == SDL_QUIT) running = false;

      if (e.type == SDL_KEYDOWN) {
        SDL_Keycode k = e.key.keysym.sym;
        char *last_turn = " ";

        /*
        end_time = time(NULL);
        redisReply *reply = redisCommand(
          c,
          "HSET team3fmission:%s:summary "
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
          end_time,                  // time_t or 0
          moves_left_turn,                         // moves_left_turn
          moves_right_turn,                         // moves_right_turn
          moves_straight,                         // moves_straight
          moves_reverse,                         // moves_reverse
          moves_left_turn + moves_right_turn + moves_straight + moves_reverse,   // moves_total
          sqrt(px*px + py*py),                       // distance_traveled
          end_time - start_time,                        // duration_seconds
          won ? "success" : "aborted",                 // mission_result
          "user exited"              // abort_reason
        );

        char* json = create_mission_summary_json(
        mission_id,
        robot_id,
        mission_type,
        start_time,
        end_time,
        moves_left_turn,
        moves_right_turn,
        moves_straight,
        moves_reverse,
        moves_left_turn + moves_right_turn + moves_straight + moves_reverse,
        sqrt(px*px + py*py),
        end_time - start_time,
        won ? "success" : "aborted",
        "user exited"
    );

    post_json_to_move(
        "https://10.170.8.109:8449/move",
        json
    );
    */

    //free(json);

        if (k == SDLK_ESCAPE) {
          end_time =time(NULL);

          printf("ESC SAVE left=%d right=%d straight=%d reverse=%d total=%d distance=%.2f duration=%ld\n",
                 moves_left_turn, 
                 moves_right_turn, 
                 moves_straight, 
                 moves_reverse,
                 moves_left_turn + moves_right_turn + moves_straight + moves_reverse,
                 distance_traveled,
                 end_time - start_time);

          redisReply *reply = redisCommand(
            c,
            "HSET team3fmission:%s:summary "
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
            end_time,                  // time_t or 0
            moves_left_turn,                         // moves_left_turn
            moves_right_turn,                         // moves_right_turn
            moves_straight,                         // moves_straight
            moves_reverse,                         // moves_reverse
            moves_left_turn + moves_right_turn + moves_straight + moves_reverse,   //
            distance_traveled,                       // distance_traveled
            end_time - start_time,                        // duration_seconds
            won ? "success" : "aborted",                 // mission_result
            "user exited"              // abort_reason
          );
          if (reply) freeReplyObject(reply);

          if (learning_redis && !learning_redis->err) {
              learning_mark_dead_ends(&learned);
              learning_save(learning_redis, maze_id, &learned);

              printf("Learning save: known=%d dead_ends=%d reused=%d\n",
               learned.known_cells,
               learned.dead_ends_marked,
               learned.reused_prior_knowledge
              );

          }
          char*json = create_mission_summary_json(
            mission_id,
            robot_id,
            mission_type,
            start_time,
            end_time,
            moves_left_turn,
            moves_right_turn,
            moves_straight,
            moves_reverse,
            moves_left_turn + moves_right_turn + moves_straight + moves_reverse,
            distance_traveled,
            end_time - start_time,
            won ? "success" : "aborted",
            "user exited"
        );

        if (json) {
          post_json_to_move(
            "https://10.170.8.109:8449/move",
            json
          );
          free(json);

        }

        printf(
          "Mission %s for robot %s was a %s mission."
          "It recorded %d total moves, including %d left turns, %d right turns, %d straight moves, and %d reverse moves."
          "The robot traveled a distance of %.2f units over %ld seconds.\n"
          "The mission result was: %s. Abort reason: %s.\n",
          mission_id,
          robot_id,
          mission_type,
          moves_left_turn + moves_right_turn + moves_straight + moves_reverse,
          moves_left_turn,
          moves_right_turn,
          moves_straight,
          moves_reverse,
          distance_traveled,
          end_time - start_time,
          won ? "success" : "aborted",
          won ? "none" : "user exited"
        );

          running = false;
        }

        if (k == SDLK_l) {
          
          if (!save_maze_data("maze_data.json",
                    mission_id,
                    px,
                    py,
                    orientation)) {
                fprintf(stderr, "Failed to write maze_data.json\n");
            }
          execl("./missions/mission_dashboard", "mission_dashboard", mission_id, NULL);
        }

        if (k == SDLK_n) {
          hasMoved = do_learning_ai_step(&px, &py, &orientation,
                                   &moves_left_turn, &moves_right_turn,
                                   &moves_straight, &moves_reverse,
                                   &last_turn);

          if (px == MAZE_W - 1 && py == MAZE_H - 1) {
            won = true;
            SDL_SetWindowTitle(win, "You win! Press R to regenerate, Esc to quit");
        }
    }
        

        
        
        if (k == SDLK_r) {
          regenerate(&px, &py, win);
          save_maze_grid("maze_grid.dat");
          won = false;
        }
        if (!won) {
          if (k == SDLK_UP || k == SDLK_w) 
          {
            hasMoved = try_move(&px, &py, 0, -1);
            if (hasMoved)
            {
              if (orientation == 'N')
              {
                moves_straight += 1;
                last_turn = "forward";
              }
              else if (orientation == 'E')
              {
                moves_left_turn += 1;
                last_turn = "left";
              }
              else if (orientation == 'S')
              {
                moves_reverse += 1;
                last_turn = "backward";
              }
              else if (orientation == 'W')
              {
                moves_right_turn += 1;
                last_turn = "right";
              }
              orientation = 'N';
            }
          }
          if (k == SDLK_RIGHT || k == SDLK_d)
          {
            hasMoved = try_move(&px, &py, 1, 0);
            if (hasMoved)
            {
              if (orientation == 'N')
              {
                moves_right_turn += 1;
                last_turn = "right";
              }
              else if (orientation == 'E')
              {
                moves_straight += 1;
                last_turn = "forward";
              }
              else if (orientation == 'S')
              {
                moves_left_turn += 1;
                last_turn = "left";
              }
              else if (orientation == 'W')
              {
                moves_reverse += 1;
                last_turn = "backward";
              }
              orientation = 'E';
            }
          }
          if (k == SDLK_DOWN || k == SDLK_s)  
          {
            hasMoved = try_move(&px, &py, 0, 1);
            if (hasMoved)
            {
              if (orientation == 'N')
              {
                moves_reverse += 1;
                last_turn = "backward";
              }
              else if (orientation == 'E')
              {
                moves_right_turn += 1;
                last_turn = "right";
              }
              else if (orientation == 'S')
              {
                moves_straight += 1;
                last_turn = "forward";
              }
              else if (orientation == 'W')
              {
                moves_left_turn += 1;
                last_turn = "left";
              }
              orientation = 'S';
            }
          }
          if (k == SDLK_LEFT || k == SDLK_a)  
          {
            hasMoved = try_move(&px, &py, -1, 0);
            if (hasMoved)
            {
              if (orientation == 'N')
              {
                moves_left_turn += 1;
                last_turn = "left";
              }
              else if (orientation == 'E')
              {
                moves_reverse += 1;
                last_turn = "backward";
              }
              else if (orientation == 'S')
              {
                moves_right_turn += 1;
                last_turn = "right";
              }
              else if (orientation == 'W')
              {
                moves_straight += 1;
                last_turn = "forward";
              }
              orientation = 'W';
            }
          }

          if (px == MAZE_W - 1 && py == MAZE_H - 1) {
            won = true;
            SDL_SetWindowTitle(win, "You win! Press R to regenerate, Esc to quit");
          }

          if (hasMoved) {
              // Add received_at as an ISO8601 string in UTC (simple + human-friendly)
              time_t now = time(NULL);
              struct tm tmbuf;
            #if defined(_WIN32)
              gmtime_s(&tmbuf, &now);
            #else
              gmtime_r(&now, &tmbuf);
            #endif
              char ts[32];
              strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmbuf);
            moveSequence += 1;
            distance_traveled += 1.0; // assuming each move is 1 unit; adjust if needed
            create_player_move_json(
            "player_move",
            "joystick",
            moveSequence,
            px,
            py,
            won,
            ts,
            last_turn,
            json,
            JSON_BUFFER_SIZE
            );
            post_json_to_move(URL_ENDPOINT_LOGGING, json);
            post_json_to_move(URL_ENDPOINT_MP, json);
            print_pretty_json(json);

            printf("ABOUT TO UPDATE REDIS SUMMARY\n");

            
            end_time = time(NULL);
            redisReply *reply = redisCommand(
            c,
            "HSET team3fmission:%s:summary "
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
            end_time,                  // time_t or 0
            moves_left_turn,                         // moves_left_turn
            moves_right_turn,                         // moves_right_turn
            moves_straight,                         // moves_straight
            moves_reverse,                         // moves_reverse
            moves_left_turn + moves_right_turn + moves_straight + moves_reverse,   //
            distance_traveled,                       // distance_traveled
            end_time - start_time,                        // duration_seconds
            won ? "success" : "in_progress",                 // mission_result
            "player moved"              // abort_reason
            );
            if (!reply) {              
              printf("Failed to update Redis summary\n");
            }
            else {
              printf(
                "Updated Redis summary: end_time=%ld, moves_left_turn=%d, moves_right_turn=%d, moves_straight=%d, moves_reverse=%d, moves_total=%d, distance_traveled=%.2f, duration_seconds=%ld, mission_result=%s\n",
                end_time,
                moves_left_turn,
                moves_right_turn,
                moves_straight,
                moves_reverse,
                moves_left_turn + moves_right_turn + moves_straight + moves_reverse,
                distance_traveled,
                end_time - start_time,
                won ? "success" : "in_progress"
              );
              freeReplyObject(reply);
            }
            }
        }
      }
    }

    draw_maze(r);
    draw_player_goal(r, px, py);

    SDL_RenderPresent(r);
  }

  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}