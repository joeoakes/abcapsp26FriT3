// missions/mission_dashboard.c
// Mission Dashboard (terminal) for Mini-Pupper / Maze missions.
//
// Purpose:
//   - Launched by the Maze SDL2 app when Left Trigger / L button is pressed.
//   - Reads mission summary from Redis hash: mission:{mission_id}:summary
//   - Prints a clean "mission report" to the terminal.
//
// Build (with Redis support):
//   sudo apt-get install -y gcc make libhiredis-dev
//   make
//
// Build (without Redis; prints placeholders):
//   make NO_REDIS=1
//
// Run:
//   ./mission_dashboard <mission_id> [redis_host] [redis_port]
//
// Example:
//   ./mission_dashboard 2f1c0b5d-9d2a-4d8b-b5ad-2d7c6a0fd6b3 127.0.0.1 6379
//
// Notes:
//   - Keep this in a subfolder: ./missions/mission_dashboard
//   - Maze should launch via execl("./missions/mission_dashboard", "mission_dashboard", mission_id, NULL);

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <unistd.h>


#ifndef NO_REDIS
  #include <hiredis/hiredis.h>
#endif

void draw_kv(
    SDL_Renderer* renderer,
    TTF_Font* font,
    int x, int y,
    const char* key,
    const char* fmt,
    ...
) {
    char value[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(value, sizeof(value), fmt, args);
    va_end(args);

    char line[512];
    snprintf(line, sizeof(line), "%s: %s", key, value);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderText_Blended(font, line, white);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);

    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);

    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}


static void print_usage(const char* argv0) {
  fprintf(stderr,
    "Usage: %s <mission_id> [redis_host] [redis_port]\n"
    "  mission_id   UUID for the mission\n"
    "  redis_host   default: 127.0.0.1\n"
    "  redis_port   default: 6379\n"
    "\n"
    "Reads Redis hash: mission:{mission_id}:summary\n",
    argv0
  );
}

static const char* safe_s(const char* s) { return (s && *s) ? s : "(none)"; }

static void print_header(void) {
  printf("============================================================\n");
  printf("                  MINI-PUPPER MISSION REPORT\n");
  printf("============================================================\n");
}

static void print_kv(const char* k, const char* v) {
  printf("%-20s : %s\n", k, safe_s(v));
}

static void print_footer(void) {
  printf("============================================================\n");
}

#ifndef NO_REDIS
// Fetch a single field from a hash. Returns malloc'd string or NULL.
static char* hget(redisContext* c, const char* key, const char* field) {
  redisReply* r = (redisReply*)redisCommand(c, "HGET %s %s", key, field);
  if (!r) return NULL;
  char* out = NULL;
  if (r->type == REDIS_REPLY_STRING) {
    out = strdup(r->str);
  }
  freeReplyObject(r);
  return out;
}
#endif

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }

  const char* mission_id = argv[1];
  const char* host = (argc >= 3) ? argv[2] : "127.0.0.1";
  int port = (argc >= 4) ? atoi(argv[3]) : 6379;

  char key[256];
  snprintf(key, sizeof(key), "mission:%s:summary", mission_id);

  print_header();
  print_kv("mission_id", mission_id);

#ifdef NO_REDIS
  (void)host; (void)port; (void)key;
  print_kv("robot_id", "(redis disabled)");
  print_kv("mission_type", "(redis disabled)");
  print_kv("start_time", "(redis disabled)");
  print_kv("end_time", "(redis disabled)");
  print_kv("moves_left_turn", "(redis disabled)");
  print_kv("moves_right_turn", "(redis disabled)");
  print_kv("moves_straight", "(redis disabled)");
  print_kv("moves_reverse", "(redis disabled)");
  print_kv("moves_total", "(redis disabled)");
  print_kv("distance_traveled", "(redis disabled)");
  print_kv("duration_seconds", "(redis disabled)");
  print_kv("mission_result", "(redis disabled)");
  print_kv("abort_reason", "(redis disabled)");
  print_footer();
  return 0;
#else
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;

  redisContext* c = redisConnectWithTimeout(host, port, tv);
  if (!c || c->err) {
    fprintf(stderr, "ERROR: Redis connection failed to %s:%d\n", host, port);
    if (c && c->errstr) fprintf(stderr, "  %s\n", c->errstr);
    if (c) redisFree(c);

    printf("\nNOTE: If Redis isn't running, start it:\n");
    printf("  sudo service redis-server start\n");
    printf("or run:\n");
    printf("  redis-server\n");
    print_footer();
    return 1;
  }

  // Pull fields (matches the schema you described).
  char* robot_id          = hget(c, key, "robot_id");
  char* mission_type      = hget(c, key, "mission_type");
  char* start_time        = hget(c, key, "start_time");
  char* end_time          = hget(c, key, "end_time");
  char* m_left            = hget(c, key, "moves_left_turn");
  char* m_right           = hget(c, key, "moves_right_turn");
  char* m_straight        = hget(c, key, "moves_straight");
  char* m_reverse         = hget(c, key, "moves_reverse");
  char* m_total           = hget(c, key, "moves_total");
  char* distance          = hget(c, key, "distance_traveled");
  char* duration_seconds  = hget(c, key, "duration_seconds");
  char* mission_result    = hget(c, key, "mission_result");
  char* abort_reason      = hget(c, key, "abort_reason");

  // If the key doesn't exist, Redis will return nil for fields. Give a helpful message.
  if (!robot_id && !mission_type && !start_time && !mission_result) {
    printf("\nWARNING: No mission data found in Redis for:\n");
    printf("  %s\n\n", key);
    printf("Expected fields include: robot_id, mission_type, start_time, ...\n");
    printf("If you haven't logged the mission yet, add it from the Maze app.\n\n");
  }

  print_kv("robot_id", robot_id);
  print_kv("mission_type", mission_type);
  print_kv("start_time", start_time);
  print_kv("end_time", end_time);
  print_kv("moves_left_turn", m_left);
  print_kv("moves_right_turn", m_right);
  print_kv("moves_straight", m_straight);
  print_kv("moves_reverse", m_reverse);
  print_kv("moves_total", m_total);
  print_kv("distance_traveled", distance);
  print_kv("duration_seconds", duration_seconds);
  print_kv("mission_result", mission_result);
  print_kv("abort_reason", abort_reason);

  print_footer();

  // free(robot_id);
  // free(mission_type);
  // free(start_time);
  // free(end_time);
  // free(m_left);
  // free(m_right);
  // free(m_straight);
  // free(m_reverse);
  // free(m_total);
  // free(distance);
  // free(duration_seconds);
  // free(mission_result);
  // free(abort_reason);

  redisFree(c);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow(
      "Mission Dashboard",
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      640, 480,
      SDL_WINDOW_SHOWN
  );

  SDL_Renderer* renderer = SDL_CreateRenderer(
      window,
      -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );


  if (!window) {
      fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
  }


  TTF_Init();

  TTF_Font* font = TTF_OpenFont("DejaVuSansMono.ttf", 16);
  if (!font) {
      printf("Font error: %s\n", TTF_GetError());
  }

  while (1)
  {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_KEYDOWN) 
      {
        SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_r) {
          if (execl("./maze_sdl2", "maze_sdl2", "--tombstone", NULL) == -1) {
              char *cwd; printf("CWD = %s\n", (cwd = getcwd(NULL, 0)) ? cwd : "unknown"), free(cwd);
              perror("execl failed");
          }
        }
      }
      else if (e.type == SDL_QUIT) {
            SDL_Quit();
            exit(0);
        }
    }
    SDL_Delay(10);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int y = 20;

    draw_kv(renderer, font, 20, y+=20, "mission_id", "%s", mission_id);
    draw_kv(renderer, font, 20, y+=20, "robot_id", "%s", robot_id);
    draw_kv(renderer, font, 20, y+=20, "mission_type", "%s", mission_type);
    draw_kv(renderer, font, 20, y+=20, "start_time", "%s", start_time);
    draw_kv(renderer, font, 20, y+=20, "end_time", "%s", end_time);

    draw_kv(renderer, font, 20, y+=20, "moves_left_turn", "%s", m_left);
    draw_kv(renderer, font, 20, y+=20, "moves_right_turn", "%s", m_right);
    draw_kv(renderer, font, 20, y+=20, "moves_straight", "%s", m_straight);
    draw_kv(renderer, font, 20, y+=20, "moves_reverse", "%s", m_reverse);
    draw_kv(renderer, font, 20, y+=20, "moves_total", "%s", m_total);

    draw_kv(renderer, font, 20, y+=20, "distance_traveled", "%s", distance);
    draw_kv(renderer, font, 20, y+=20, "duration_seconds", "%s", duration_seconds);

    draw_kv(renderer, font, 20, y+=20, "mission_result", "%s", mission_result);
    draw_kv(renderer, font, 20, y+=20, "abort_reason", "%s", abort_reason);


    SDL_RenderPresent(renderer);

  }
  return 0;
#endif
}
