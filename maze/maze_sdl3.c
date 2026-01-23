// maze_sdl2.c
// Simple SDL2 maze with JSON output of pupperbot coordinates

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAZE_W 21
#define MAZE_H 15
#define CELL   32
#define PAD    16

// Wall bitmask
enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
  uint8_t walls;
  bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

static inline bool in_bounds(int x, int y) {
  return (x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H);
}

/* ---------------- JSON OUTPUT ---------------- */

static void output_pupperbot_json(int x, int y) {
  printf("{\"pupperbot\":{\"x\":%d,\"y\":%d}}\n", x, y);
  fflush(stdout);
}

/* ---------------- MAZE LOGIC ---------------- */

static void knock_down(int x, int y, int nx, int ny) {
  if (nx == x && ny == y - 1) {
    g[y][x].walls &= ~WALL_N;
    g[ny][nx].walls &= ~WALL_S;
  } else if (nx == x + 1 && ny == y) {
    g[y][x].walls &= ~WALL_E;
    g[ny][nx].walls &= ~WALL_W;
  } else if (nx == x && ny == y + 1) {
    g[y][x].walls &= ~WALL_S;
    g[ny][nx].walls &= ~WALL_N;
  } else if (nx == x - 1 && ny == y) {
    g[y][x].walls &= ~WALL_W;
    g[ny][nx].walls &= ~WALL_E;
  }
}

static void maze_init(void) {
  for (int y = 0; y < MAZE_H; y++)
    for (int x = 0; x < MAZE_W; x++) {
      g[y][x].walls = WALL_N | WALL_E | WALL_S | WALL_W;
      g[y][x].visited = false;
    }
}

static void maze_generate(int sx, int sy) {
  typedef struct { int x, y; } P;
  P stack[MAZE_W * MAZE_H];
  int top = 0;

  g[sy][sx].visited = true;
  stack[top++] = (P){sx, sy};

  while (top > 0) {
    P cur = stack[top - 1];
    int x = cur.x, y = cur.y;

    P neigh[4];
    int n = 0;
    const int dx[4] = {0,1,0,-1};
    const int dy[4] = {-1,0,1,0};

    for (int i = 0; i < 4; i++) {
      int nx = x + dx[i], ny = y + dy[i];
      if (in_bounds(nx, ny) && !g[ny][nx].visited)
        neigh[n++] = (P){nx, ny};
    }

    if (n == 0) { top--; continue; }

    P next = neigh[rand() % n];
    knock_down(x, y, next.x, next.y);
    g[next.y][next.x].visited = true;
    stack[top++] = next;
  }

  for (int y = 0; y < MAZE_H; y++)
    for (int x = 0; x < MAZE_W; x++)
      g[y][x].visited = false;
}

/* ---------------- RENDERING ---------------- */

static void draw_maze(SDL_Renderer* r) {
  SDL_SetRenderDrawColor(r, 15, 15, 18, 255);
  SDL_RenderClear(r);

  SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
  int ox = PAD, oy = PAD;

  for (int y = 0; y < MAZE_H; y++)
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

static void draw_player_goal(SDL_Renderer* r, int px, int py) {
  int ox = PAD, oy = PAD;

  SDL_Rect goal = {
    ox + (MAZE_W - 1) * CELL + 6,
    oy + (MAZE_H - 1) * CELL + 6,
    CELL - 12, CELL - 12
  };
  SDL_SetRenderDrawColor(r, 40, 160, 70, 255);
  SDL_RenderFillRect(r, &goal);

  SDL_Rect p = {
    ox + px * CELL + 8,
    oy + py * CELL + 8,
    CELL - 16, CELL - 16
  };
  SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
  SDL_RenderFillRect(r, &p);
}

/* ---------------- MOVEMENT ---------------- */

static bool try_move(int* px, int* py, int dx, int dy) {
  int x = *px, y = *py;
  int nx = x + dx, ny = y + dy;
  if (!in_bounds(nx, ny)) return false;

  uint8_t w = g[y][x].walls;
  if (dx == 0 && dy == -1 && (w & WALL_N)) return false;
  if (dx == 1 && dy == 0  && (w & WALL_E)) return false;
  if (dx == 0 && dy == 1  && (w & WALL_S)) return false;
  if (dx == -1 && dy == 0 && (w & WALL_W)) return false;

  *px = nx;
  *py = ny;
  return true;
}

static void regenerate(int* px, int* py, SDL_Window* win) {
  maze_init();
  maze_generate(0, 0);
  *px = 0;
  *py = 0;
  SDL_SetWindowTitle(win, "SDL2 Maze - Reach the green goal");
  output_pupperbot_json(*px, *py);
}

/* ---------------- MAIN ---------------- */

int main(void) {
  srand((unsigned)time(NULL));

  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window* win = SDL_CreateWindow(
    "SDL2 Maze - Reach the green goal",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    PAD * 2 + MAZE_W * CELL,
    PAD * 2 + MAZE_H * CELL,
    SDL_WINDOW_SHOWN
  );

  SDL_Renderer* r = SDL_CreateRenderer(
    win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );

  int px = 0, py = 0;
  regenerate(&px, &py, win);

  bool running = true, won = false;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;

      if (e.type == SDL_KEYDOWN) {
        SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_ESCAPE) running = false;

        if (k == SDLK_r) {
          regenerate(&px, &py, win);
          won = false;
        }

        if (!won) {
          bool moved = false;

          if (k == SDLK_UP || k == SDLK_w)
            moved = try_move(&px, &py, 0, -1);
          else if (k == SDLK_RIGHT || k == SDLK_d)
            moved = try_move(&px, &py, 1, 0);
          else if (k == SDLK_DOWN || k == SDLK_s)
            moved = try_move(&px, &py, 0, 1);
          else if (k == SDLK_LEFT || k == SDLK_a)
            moved = try_move(&px, &py, -1, 0);

          if (moved)
            output_pupperbot_json(px, py);

          if (px == MAZE_W - 1 && py == MAZE_H - 1) {
            won = true;
            SDL_SetWindowTitle(win, "You win! Press R to regenerate");
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
