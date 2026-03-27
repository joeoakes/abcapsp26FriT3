#ifndef MAZE_LEARNING_H
#define MAZE_LEARNING_H

#include <stdbool.h>
#include <hiredis/hiredis.h>

#define MAZE_W 21
#define MAZE_H 15

typedef struct {
    unsigned char walls;
    bool visited;
} LearnedCell;

enum {
    L_UNKNOWN = -1,
    L_OPEN    = 0,
    L_WALL    = 1,
    L_DEAD    = 2,
    L_GOAL    = 3
};

typedef struct {
    int cells[MAZE_H][MAZE_W];
    int visit_count[MAZE_H][MAZE_W];
    int run_number;
    int known_cells;
    int dead_ends_marked;
    int reused_prior_knowledge;
} LearnedMaze;



void learning_init(LearnedMaze *lm);
int  learning_load(redisContext *rc, const char *maze_id, LearnedMaze *lm);
int  learning_save(redisContext *rc, const char *maze_id, const LearnedMaze *lm);

void learning_mark_open(LearnedMaze *lm, int x, int y);
void learning_mark_wall(LearnedMaze *lm, int x, int y);
void learning_mark_goal(LearnedMaze *lm, int x, int y);
void learning_mark_visit(LearnedMaze *lm, int x, int y);
void learning_mark_dead_ends(LearnedMaze *lm);

char learning_next_move(LearnedMaze *lm, int px, int py, int goalx, int goaly);
char learned_astar_next_move(LearnedMaze *lm, int px, int py, int goalx, int goaly);
#endif