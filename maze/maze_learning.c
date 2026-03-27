#include "maze_learning.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int in_bounds(int x, int y) {
    return x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H;
}

void learning_init(LearnedMaze *lm) {
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            lm->cells[y][x] = L_UNKNOWN;
            lm->visit_count[y][x] = 0;
        }
    }
    lm->run_number = 0;
    lm->known_cells = 0;
    lm->dead_ends_marked = 0;
    lm->reused_prior_knowledge = 0;
}

static void set_cell(LearnedMaze *lm, int x, int y, int value) {
    if (!in_bounds(x, y)) return;
    if (lm->cells[y][x] == L_UNKNOWN && value != L_UNKNOWN) {
        lm->known_cells++;
    }
    lm->cells[y][x] = value;
}

void learning_mark_open(LearnedMaze *lm, int x, int y) {
    set_cell(lm, x, y, L_OPEN);
}

void learning_mark_wall(LearnedMaze *lm, int x, int y) {
    set_cell(lm, x, y, L_WALL);
}

void learning_mark_goal(LearnedMaze *lm, int x, int y) {
    set_cell(lm, x, y, L_GOAL);
}

void learning_mark_visit(LearnedMaze *lm, int x, int y) {
    if (!in_bounds(x, y)) return;
    if (lm->cells[y][x] == L_UNKNOWN) {
        lm->cells[y][x] = L_OPEN;
        lm->known_cells++;
    }
    lm->visit_count[y][x]++;
}

void learning_mark_dead_ends(LearnedMaze *lm) {
    int dx[4] = {0, 1, 0, -1};
    int dy[4] = {-1, 0, 1, 0};

    lm->dead_ends_marked = 0;

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            if (lm->cells[y][x] != L_OPEN) continue;

            int exits = 0;
            for (int i = 0; i < 4; i++) {
                int nx = x + dx[i];
                int ny = y + dy[i];
                if (!in_bounds(nx, ny)) continue;
                if (lm->cells[ny][nx] == L_OPEN || lm->cells[ny][nx] == L_GOAL)
                    exits++;
            }

            if (exits <= 1) {
                lm->cells[y][x] = L_DEAD;
                lm->dead_ends_marked++;
            }
        }
    }
}

typedef struct {
    int x;
    int y;
    int g;
    int f;
    int parent_x;
    int parent_y;
} LNode;

static int l_in_bounds(int x, int y) {
    return x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H;
}

static int l_manhattan(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

char learned_astar_next_move(LearnedMaze *lm, int px, int py, int goalx, int goaly) {
    LNode open[MAZE_W * MAZE_H];
    int open_count = 0;
    int closed[MAZE_H][MAZE_W] = {0};
    int gscore[MAZE_H][MAZE_W];
    int parent_x[MAZE_H][MAZE_W];
    int parent_y[MAZE_H][MAZE_W];

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            gscore[y][x] = 999999;
            parent_x[y][x] = -1;
            parent_y[y][x] = -1;
        }
    }

    open[0] = (LNode){px, py, 0, l_manhattan(px, py, goalx, goaly), -1, -1};
    open_count = 1;
    gscore[py][px] = 0;

    while (open_count > 0) {
        int best = 0;
        for (int i = 1; i < open_count; i++) {
            if (open[i].f < open[best].f) best = i;
        }

        LNode cur = open[best];
        open[best] = open[--open_count];

        if (closed[cur.y][cur.x]) continue;
        closed[cur.y][cur.x] = 1;

        if (cur.x == goalx && cur.y == goaly) {
            int cx = goalx;
            int cy = goaly;

            while (!(parent_x[cy][cx] == px && parent_y[cy][cx] == py)) {
                int tx = parent_x[cy][cx];
                int ty = parent_y[cy][cx];
                if (tx == -1 || ty == -1) return 0;
                cx = tx;
                cy = ty;
            }

            if (cx == px && cy == py - 1) return 'N';
            if (cx == px + 1 && cy == py) return 'E';
            if (cx == px && cy == py + 1) return 'S';
            if (cx == px - 1 && cy == py) return 'W';
            return 0;
        }

        int dx[4] = {0, 1, 0, -1};
        int dy[4] = {-1, 0, 1, 0};

        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dx[i];
            int ny = cur.y + dy[i];

            if (!l_in_bounds(nx, ny)) continue;
            if (closed[ny][nx]) continue;

            if (lm->cells[ny][nx] == L_WALL) continue;
            int cost = 1;
            if (lm->cells[ny][nx] == L_UNKNOWN) cost += 3;
            else if (lm->cells[ny][nx] == L_OPEN) cost += 1;
            
            else if (lm->cells[ny][nx] == L_GOAL) cost += 1;
            else if (lm->cells[ny][nx] == L_DEAD) cost += 6;

            int tentative_g = cur.g + cost;

            if (tentative_g < gscore[ny][nx]) {
                gscore[ny][nx] = tentative_g;
                parent_x[ny][nx] = cur.x;
                parent_y[ny][nx] = cur.y;

                open[open_count++] = (LNode){
                    nx,
                    ny,
                    tentative_g,
                    tentative_g + l_manhattan(nx, ny, goalx, goaly),
                    cur.x,
                    cur.y
                };
            }
        }
    }

    return 0;
}

int learning_load(redisContext *rc, const char *maze_id, LearnedMaze *lm) {
    learning_init(lm);
    if (!rc || !maze_id) return 0;

    char key[128];
    snprintf(key, sizeof(key), "team3fmaze:%s:learned", maze_id);

    redisReply *reply = redisCommand(rc, "GET %s", key);
    if (!reply) return 0;

    if (reply->type != REDIS_REPLY_STRING || !reply->str) {
        freeReplyObject(reply);
        return 0;
    }

    const char *p = reply->str;
    int idx = 0;

    while (*p && idx < MAZE_W * MAZE_H) {
        lm->cells[idx / MAZE_W][idx % MAZE_W] = atoi(p);
        if (lm->cells[idx / MAZE_W][idx % MAZE_W] != L_UNKNOWN) {
            lm->known_cells++;
            lm->reused_prior_knowledge++;
        }

        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        idx++;
    }

    freeReplyObject(reply);

    redisReply *r2 = redisCommand(rc, "INCR team3fmaze:%s:runs", maze_id);
    if (r2) {
        if (r2->type == REDIS_REPLY_INTEGER) lm->run_number = (int)r2->integer;
        freeReplyObject(r2);
    }

    return 1;
}

int learning_save(redisContext *rc, const char *maze_id, const LearnedMaze *lm) {
    if (!rc || !maze_id || !lm) return 0;

    char key[128];
    snprintf(key, sizeof(key), "team3fmaze:%s:learned", maze_id);

    char buffer[8192];
    buffer[0] = '\0';

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d", lm->cells[y][x]);
            strcat(buffer, tmp);
            if (!(y == MAZE_H - 1 && x == MAZE_W - 1)) strcat(buffer, ",");
        }
    }

    redisReply *reply = redisCommand(rc, "SET %s %s", key, buffer);
    if (!reply) return 0;
    freeReplyObject(reply);
    return 1;
}

char learning_next_move(LearnedMaze *lm, int px, int py, int goalx, int goaly) {
    int dx[4] = {0, 1, 0, -1};
    int dy[4] = {-1, 0, 1, 0};
    char mv[4] = {'N', 'E', 'S', 'W'};

    int best_score = -999999;
    char best_move = 0;

    for (int i = 0; i < 4; i++) {
        int nx = px + dx[i];
        int ny = py + dy[i];
        if (!in_bounds(nx, ny)) continue;
        if (lm->cells[ny][nx] == L_WALL) continue;

        int score = 0;

        if (lm->cells[ny][nx] == L_UNKNOWN) score += 100;
        if (lm->cells[ny][nx] == L_OPEN)    score += 20;
        if (lm->cells[ny][nx] == L_GOAL)    score += 1000;
        if (lm->cells[ny][nx] == L_DEAD)    score -= 1000;

        score -= lm->visit_count[ny][nx] * 100;

        int dist = abs(goalx - nx) + abs(goaly - ny);
        score -= dist;

        if (score > best_score) {
            best_score = score;
            best_move = mv[i];
        }
    }

    return best_move;
}