#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define MAZE_W 21
#define MAZE_H 15

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
    unsigned char walls;
    bool visited;
} Cell;

typedef struct {
    int x;
    int y;
    int g;
    int f;
    int parent;
} Node;

// precomputed solution
static char solution_moves[MAZE_W * MAZE_H];
static int solution_length = 0;
static int solution_index = 0;

// Manhattan distance
static int manhattan(int x1,int y1,int x2,int y2) {
    return abs(x1-x2)+abs(y1-y2);
}

// check bounds
static int in_bounds(int x,int y) {
    return x>=0 && x<MAZE_W && y>=0 && y<MAZE_H;
}

// build the solution once
static void compute_solution(Cell maze[MAZE_H][MAZE_W], int px, int py) {
    int goalx = MAZE_W-1;
    int goaly = MAZE_H-1;

    Node open[MAZE_W*MAZE_H];
    int openCount=0;
    bool closed[MAZE_H][MAZE_W] = {0};

    open[0] = (Node){px, py, 0, manhattan(px,py,goalx,goaly), -1};
    openCount = 1;

    Node came_from[MAZE_H][MAZE_W];
    for(int y=0;y<MAZE_H;y++)
        for(int x=0;x<MAZE_W;x++)
            came_from[y][x] = (Node){-1,-1,0,0,-1};

    Node end_node = {-1,-1,0,0,-1};

    while(openCount > 0) {
        int best=0;
        for(int i=1;i<openCount;i++)
            if(open[i].f < open[best].f)
                best=i;

        Node cur = open[best];
        open[best] = open[--openCount];

        if(cur.x == goalx && cur.y == goaly) {
            end_node = cur;
            break;
        }

        closed[cur.y][cur.x] = true;

        int dx[4] = {0,1,0,-1};
        int dy[4] = {-1,0,1,0};
        int wall[4] = {WALL_N, WALL_E, WALL_S, WALL_W};

        for(int i=0;i<4;i++) {
            int nx = cur.x + dx[i];
            int ny = cur.y + dy[i];

            if(!in_bounds(nx,ny)) continue;
            if(maze[cur.y][cur.x].walls & wall[i]) continue;
            if(closed[ny][nx]) continue;

            Node n = {nx, ny, cur.g+1, cur.g+1 + manhattan(nx,ny,goalx,goaly), 0};
            n.parent = cur.x + cur.y * MAZE_W;

            open[openCount++] = n;
            came_from[ny][nx] = cur;
        }
    }

    // reconstruct path
    solution_length = 0;
    solution_index = 0;
    if(end_node.x == -1) return; // no path found

    Node n = end_node;
    while(!(n.x == px && n.y == py)) {
        Node p = came_from[n.y][n.x];

        if(n.x > p.x) solution_moves[solution_length++] = 'E';
        else if(n.x < p.x) solution_moves[solution_length++] = 'W';
        else if(n.y > p.y) solution_moves[solution_length++] = 'S';
        else if(n.y < p.y) solution_moves[solution_length++] = 'N';

        n = p;
    }

    // reverse the path to start from player
    for(int i=0;i<solution_length/2;i++) {
        char tmp = solution_moves[i];
        solution_moves[i] = solution_moves[solution_length-1-i];
        solution_moves[solution_length-1-i] = tmp;
    }
}

// returns the next move
char astar_next_move(Cell maze[MAZE_H][MAZE_W], int px, int py) {
    // if no solution yet, compute it
    if(solution_length == 0) compute_solution(maze, px, py);

    if(solution_index >= solution_length) return 0; // at goal

    return solution_moves[solution_index++];
}