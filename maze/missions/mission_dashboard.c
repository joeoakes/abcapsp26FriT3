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
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, color);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

// ====================== BAR ======================

void draw_bar(SDL_Renderer* r, int x, int y, int width, int height,
              int value, int max_value, SDL_Color fill) {
    SDL_SetRenderDrawColor(r, 50,50,50,255);
    SDL_Rect bg = {x, y, width, height};
    SDL_RenderFillRect(r, &bg);

    int fill_width = (max_value>0) ? value * width / max_value : 0;
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
    SDL_Rect fg = {x, y, fill_width, height};
    SDL_RenderFillRect(r, &fg);
}

// ====================== REDIS ======================

#ifndef NO_REDIS
static char* hget(redisContext* c, const char* key, const char* field) {
    redisReply* r = redisCommand(c, "HGET %s %s", key, field);
    if (!r) return NULL;

    char* out = NULL;
    if (r->type == REDIS_REPLY_STRING)
        out = strdup(r->str);

    freeReplyObject(r);
    return out;
}
#endif

// ====================== MAIN ======================

int main(int argc, char** argv) {

    if (argc < 2) {
        printf("Usage: %s <mission_id>\n", argv[0]);
        return 1;
    }

    const char* mission_id = argv[1];

#ifndef NO_REDIS
    redisContext* c = redisConnect("127.0.0.1", 6379);

    char key[256];
    snprintf(key, sizeof(key), "team3fmission:%s:summary", mission_id);

    char* robot_id = hget(c,key,"robot_id");
    char* mission_type = hget(c,key,"mission_type");
    char* start_time = hget(c,key,"start_time");
    char* end_time = hget(c,key,"end_time");
    char* distance = hget(c,key,"distance_traveled");
    char* duration = hget(c,key,"duration_seconds");
    char* result = hget(c,key,"mission_result");
    char* abort_reason = hget(c,key,"abort_reason");

    char* m_left = hget(c,key,"moves_left_turn");
    char* m_right = hget(c,key,"moves_right_turn");
    char* m_straight = hget(c,key,"moves_straight");
    char* m_reverse = hget(c,key,"moves_reverse");
    char* m_total = hget(c,key,"moves_total");
#else
    char* robot_id="pupper";
    char* mission_type="maze";
    char* start_time="12:00";
    char* end_time="12:01";
    char* distance="12";
    char* duration="30";
    char* result="success";
    char* abort_reason="none";

    char* m_left="10";
    char* m_right="12";
    char* m_straight="25";
    char* m_reverse="2";
    char* m_total="49";
#endif

    SDL_Color green={0,200,0,255};
    SDL_Color red={200,0,0,255};
    SDL_Color yellow={200,200,0,255};
    SDL_Color white={255,255,255,255};
    SDL_Color blue={0,150,255,255};

    SDL_Color status_color = yellow;
    if (result && strcmp(result,"success")==0) status_color=green;
    else if (result && strcmp(result,"failed")==0) status_color=red;

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_Window* win = SDL_CreateWindow("Mission Dashboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800,600,0);

    SDL_Renderer* ren = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);

    TTF_Font* font = TTF_OpenFont("DejaVuSansMono.ttf",16);
    TTF_Font* big = TTF_OpenFont("DejaVuSansMono.ttf",28);

    while (1) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type==SDL_QUIT) exit(0);

            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_r) {
                    execl("./maze_sdl2", "maze_sdl2", "--tombstone", NULL);
                }
            }
        }

        SDL_SetRenderDrawColor(ren,20,20,20,255);
        SDL_RenderClear(ren);

        // HEADER
        draw_text(ren,big,20,10,"MISSION DASHBOARD",white);
        draw_text(ren,font,20,50,mission_id,(SDL_Color){180,180,180,255});

        // LEFT PANEL – mission info
        int panel_x = 20;
        int y = 100;
        #define DRAW_FIELD(label,val) \
            draw_text(ren,font,panel_x,y+=30,label,(SDL_Color){200,200,200,255}); \
            draw_text(ren,font,panel_x+120,y,safe_s(val),white);

        DRAW_FIELD("Robot:", robot_id);
        DRAW_FIELD("Type:", mission_type);
        DRAW_FIELD("Start:", start_time);
        DRAW_FIELD("End:", end_time);
        DRAW_FIELD("Duration:", duration);
        DRAW_FIELD("Distance:", distance);
        DRAW_FIELD("Abort:", abort_reason);
        DRAW_FIELD("Result:", result);

        // RIGHT PANEL – move bars with numbers
        int right_x = 360;
        int bar_y = 100;
        int bar_width = 380;
        int bar_height = 20;
        int max_moves = atoi(m_total);
        if(max_moves<=0) max_moves=1;

        // Left
        draw_bar(ren,right_x,bar_y,bar_width,bar_height,atoi(m_left),max_moves,blue);
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",atoi(m_left));
        draw_text(ren,font,right_x+bar_width+10,bar_y,buf,white);
        draw_text(ren,font,right_x,bar_y-20,"Left",white);

        // Right
        draw_bar(ren,right_x,bar_y+50,bar_width,bar_height,atoi(m_right),max_moves,blue);
        snprintf(buf,sizeof(buf),"%d",atoi(m_right));
        draw_text(ren,font,right_x+bar_width+10,bar_y+50,buf,white);
        draw_text(ren,font,right_x,bar_y+30,"Right",white);

        // Straight
        draw_bar(ren,right_x,bar_y+100,bar_width,bar_height,atoi(m_straight),max_moves,blue);
        snprintf(buf,sizeof(buf),"%d",atoi(m_straight));
        draw_text(ren,font,right_x+bar_width+10,bar_y+100,buf,white);
        draw_text(ren,font,right_x,bar_y+80,"Straight",white);

        // Reverse
        draw_bar(ren,right_x,bar_y+150,bar_width,bar_height,atoi(m_reverse),max_moves,blue);
        snprintf(buf,sizeof(buf),"%d",atoi(m_reverse));
        draw_text(ren,font,right_x+bar_width+10,bar_y+150,buf,white);
        draw_text(ren,font,right_x,bar_y+130,"Reverse",white);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    return 0;
}