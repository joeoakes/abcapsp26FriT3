
// missions/mission_dashboard.c
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <unistd.h>

#ifndef NO_REDIS
#include <hiredis/hiredis.h>
#endif

// ====================== UTIL ======================

static const char* safe_s(const char* s) {
    return (s && *s) ? s : "(none)";
}

// ====================== TEXT ======================

void draw_text(SDL_Renderer* r, TTF_Font* font,
               int x, int y, const char* text, SDL_Color color) {
    if (!font || !text) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

// ====================== ROUNDED RECT ======================

void draw_rounded_rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

// ====================== BAR ======================

void draw_bar(SDL_Renderer* r, int x, int y, int width, int height,
              int value, int max_value, SDL_Color fill) {
    SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
    SDL_Rect bg = {x, y, width, height};
    SDL_RenderFillRect(r, &bg);
    int fill_width = (max_value > 0) ? value * width / max_value : 0;
    if (fill_width > 0) {
        SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
        SDL_Rect fg = {x, y, fill_width, height};
        SDL_RenderFillRect(r, &fg);
    }
    SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
    SDL_RenderDrawRect(r, &bg);
}

// ====================== DIVIDER ======================

void draw_divider(SDL_Renderer* r, int x, int y, int width) {
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    SDL_RenderDrawLine(r, x, y, x + width, y);
}

// ====================== REDIS ======================

#ifndef NO_REDIS
static char* hget(redisContext* c, const char* key, const char* field) {
    redisReply* rep = redisCommand(c, "HGET %s %s", key, field);
    if (!rep) return NULL;
    char* out = NULL;
    if (rep->type == REDIS_REPLY_STRING)
        out = strdup(rep->str);
    freeReplyObject(rep);
    return out;
}
#endif

// ====================== MAIN ======================

int main(int argc, char** argv) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mission_id> [redis_host] [redis_port]\n", argv[0]);
        return 1;
    }

    const char* mission_id = argv[1];
    const char* redis_host = (argc >= 3) ? argv[2] : "127.0.0.1";
    int         redis_port = (argc >= 4) ? atoi(argv[3]) : 6379;

    char* robot_id     = NULL;
    char* mission_type = NULL;
    char* start_time   = NULL;
    char* end_time     = NULL;
    char* distance     = NULL;
    char* duration     = NULL;
    char* result       = NULL;
    char* abort_reason = NULL;
    char* m_left       = NULL;
    char* m_right      = NULL;
    char* m_straight   = NULL;
    char* m_reverse    = NULL;
    char* m_total      = NULL;
    int redis_ok = 0;

#ifndef NO_REDIS
    redisContext* rc = redisConnect(redis_host, redis_port);
    if (!rc || rc->err) {
        fprintf(stderr, "Redis connection error: %s\n", rc ? rc->errstr : "unknown");
    } else {
        redis_ok = 1;
        char key[256];
        snprintf(key, sizeof(key), "team3fmission:%s:summary", mission_id);
        robot_id     = hget(rc, key, "robot_id");
        mission_type = hget(rc, key, "mission_type");
        start_time   = hget(rc, key, "start_time");
        end_time     = hget(rc, key, "end_time");
        distance     = hget(rc, key, "distance_traveled");
        duration     = hget(rc, key, "duration_seconds");
        result       = hget(rc, key, "mission_result");
        abort_reason = hget(rc, key, "abort_reason");
        m_left       = hget(rc, key, "moves_left_turn");
        m_right      = hget(rc, key, "moves_right_turn");
        m_straight   = hget(rc, key, "moves_straight");
        m_reverse    = hget(rc, key, "moves_reverse");
        m_total      = hget(rc, key, "moves_total");
    }
#else
    robot_id     = "mini-pupper-01";
    mission_type = "patrol";
    start_time   = "2026-02-01T10:00:00";
    end_time     = "2026-02-01T10:03:12";
    distance     = "62";
    duration     = "192";
    result       = "success";
    abort_reason = "";
    m_left       = "10";
    m_right      = "8";
    m_straight   = "42";
    m_reverse    = "2";
    m_total      = "62";
    redis_ok     = 1;
#endif

    SDL_Color white   = {255, 255, 255, 255};
    SDL_Color gray    = {180, 180, 180, 255};
    SDL_Color dimgray = {100, 100, 100, 255};
    SDL_Color green   = {  0, 200,   0, 255};
    SDL_Color red     = {220,  50,  50, 255};
    SDL_Color yellow  = {220, 200,   0, 255};
    SDL_Color blue    = { 55, 140, 220, 255};
    SDL_Color teal    = { 30, 160, 120, 255};
    SDL_Color orange  = {210, 100,  30, 255};

    SDL_Color status_color = yellow;
    const char* status_label = "UNKNOWN";
    if (result && strcmp(result, "success") == 0) {
        status_color = green;  status_label = "SUCCESS";
    } else if (result && strcmp(result, "failed") == 0) {
        status_color = red;    status_label = "FAILED";
    } else if (result && strcmp(result, "aborted") == 0) {
        status_color = orange; status_label = "ABORTED";
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        return 1;
    }

    char win_title[300];
    snprintf(win_title, sizeof(win_title), "Mission Dashboard - %s", mission_id);

    SDL_Window*   win = SDL_CreateWindow(win_title,
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            820, 620, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    TTF_Font* font = TTF_OpenFont("../DejaVuSansMono.ttf", 15);
    TTF_Font* big  = TTF_OpenFont("../DejaVuSansMono.ttf", 26);
    TTF_Font* tiny = TTF_OpenFont("../DejaVuSansMono.ttf", 12);

    if (!font || !big || !tiny) {
        fprintf(stderr, "Font load error: %s\n", TTF_GetError());
        font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15);
        big  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 26);
        tiny = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12);
        if (!font || !big || !tiny) {
            fprintf(stderr, "Could not load any font. Exiting.\n");
            return 1;
        }
    }

    while (1) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto cleanup;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_r:
                        execl("../maze_sdl2", "maze_sdl2", "--tombstone", NULL);
                        break;
                    case SDLK_q:
                    case SDLK_ESCAPE:
                        goto cleanup;
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 18, 18, 20, 255);
        SDL_RenderClear(ren);

        int W = 820;
        int margin = 20;

        draw_text(ren, big, margin, 14, "MISSION DASHBOARD", white);

        char id_display[64];
        snprintf(id_display, sizeof(id_display), "ID: %.48s", mission_id);
        draw_text(ren, tiny, margin, 48, id_display, dimgray);

        {
            int bw = 110, bh = 28, bx = W - margin - bw, by = 18;
            draw_rounded_rect(ren, bx, by, bw, bh, (SDL_Color){30,30,30,255});
            draw_rounded_rect(ren, bx, by, 4, bh, status_color);
            draw_text(ren, font, bx + 12, by + 6, status_label, status_color);
        }

        {
            SDL_Color rc_col = redis_ok ? (SDL_Color){0,180,0,255} : (SDL_Color){180,0,0,255};
            const char* rc_str = redis_ok ? "REDIS OK" : "NO REDIS";
            draw_text(ren, tiny, W - margin - 90, 52, rc_str, rc_col);
        }

        draw_divider(ren, margin, 68, W - margin * 2);

        int lx = margin;
        int ly = 82;
        int row_h = 28;

        draw_text(ren, tiny, lx, ly, "MISSION INFO", dimgray);
        ly += 18;
        draw_divider(ren, lx, ly, 340);
        ly += 8;

        #define FIELD(label, val) \
            draw_text(ren, font, lx,       ly, label, gray); \
            draw_text(ren, font, lx + 130, ly, safe_s(val), white); \
            ly += row_h;

        FIELD("Robot:",    robot_id);
        FIELD("Type:",     mission_type);
        FIELD("Start:",    start_time);
        FIELD("End:",      end_time);

        char dur_buf[32] = "(none)";
        if (duration && *duration) snprintf(dur_buf, sizeof(dur_buf), "%s s", duration);
        FIELD("Duration:", dur_buf);

        char dist_buf[32] = "(none)";
        if (distance && *distance) snprintf(dist_buf, sizeof(dist_buf), "%s units", distance);
        FIELD("Distance:", dist_buf);

        char total_buf[32] = "(none)";
        if (m_total && *m_total) snprintf(total_buf, sizeof(total_buf), "%s", m_total);
        FIELD("Total Moves:", total_buf);

        SDL_Color abort_col = (abort_reason && *abort_reason) ? orange : dimgray;
        draw_text(ren, font, lx,       ly, "Abort:", gray);
        draw_text(ren, font, lx + 130, ly, safe_s(abort_reason), abort_col);
        ly += row_h;

        #undef FIELD

        int rx      = 400;
        int ry      = 82;
        int bar_w   = 360;
        int bar_h   = 16;
        int bar_gap = 44;

        draw_text(ren, tiny, rx, ry, "MOVEMENT BREAKDOWN", dimgray);
        ry += 18;
        draw_divider(ren, rx, ry, bar_w + 60);
        ry += 8;

        int max_moves = m_total ? atoi(m_total) : 1;
        if (max_moves <= 0) max_moves = 1;

        #define BAR_ROW(label, val_str, color) \
        { \
            int v = val_str ? atoi(val_str) : 0; \
            char count_buf[32]; \
            snprintf(count_buf, sizeof(count_buf), "%d", v); \
            draw_text(ren, font, rx, ry, label, gray); \
            draw_bar(ren, rx, ry + 18, bar_w, bar_h, v, max_moves, color); \
            draw_text(ren, font, rx + bar_w + 8, ry + 16, count_buf, white); \
            ry += bar_gap; \
        }

        BAR_ROW("Straight",   m_straight, blue);
        BAR_ROW("Left Turn",  m_left,     teal);
        BAR_ROW("Right Turn", m_right,    teal);
        BAR_ROW("Reverse",    m_reverse,  orange);

        #undef BAR_ROW

        if (m_total && atoi(m_total) > 0) {
            int tot = atoi(m_total);
            char pct_buf[128];
            snprintf(pct_buf, sizeof(pct_buf),
                "Fwd %d%%  Turns %d%%  Rev %d%%",
                m_straight ? atoi(m_straight)*100/tot : 0,
                ((m_left?atoi(m_left):0)+(m_right?atoi(m_right):0))*100/tot,
                m_reverse  ? atoi(m_reverse)*100/tot  : 0);
            draw_text(ren, tiny, rx, ry + 4, pct_buf, dimgray);
        }

        draw_divider(ren, margin, 580, W - margin * 2);
        char footer_left[128];
        snprintf(footer_left, sizeof(footer_left),
            "team3f  |  %s  |  %s:%d",
            safe_s(robot_id), redis_host, redis_port);
        draw_text(ren, tiny, margin, 588, footer_left, dimgray);
        draw_text(ren, tiny, W - 230, 588, "[R] Return to Maze   [Q] Quit", dimgray);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

cleanup:
    TTF_CloseFont(font);
    TTF_CloseFont(big);
    TTF_CloseFont(tiny);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

#ifndef NO_REDIS
    free(robot_id); free(mission_type); free(start_time); free(end_time);
    free(distance); free(duration); free(result); free(abort_reason);
    free(m_left); free(m_right); free(m_straight); free(m_reverse); free(m_total);
#endif

    return 0;
}
