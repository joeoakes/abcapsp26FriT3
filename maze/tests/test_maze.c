/**
 * test_maze.c — CUnit tests for Team 3F capstone
 * Covers: TC-013, TC-014, TC-015, TC-016, TC-022, TC-025, TC-026
 *
 * Build:
 *   gcc -o test_maze test_maze.c -lcunit -lm && ./test_maze
 */

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ── Replicated constants from maze_sdl2.c / astar.h ── */
#define MAZE_W 21
#define MAZE_H 15

#define WALL_N 0x01
#define WALL_E 0x02
#define WALL_S 0x04
#define WALL_W 0x08

typedef struct {
    uint8_t walls;
    bool visited;
} Cell;

/* ── Inline A* (mirrors astar.h logic for unit testing without SDL2) ── */

static int heuristic(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}

/* Minimal A* that returns 'N','E','S','W' or '\0' for no move */
static char astar_next_move_test(Cell maze[][21], int px, int py) {
    int gx = MAZE_W - 1, gy = MAZE_H - 1;
    if (px == gx && py == gy) return '\0';  /* already at goal */

    typedef struct { int x, y, px, py; int g, f; } Node;
    static Node open[MAZE_W * MAZE_H];
    static Node closed[MAZE_W * MAZE_H];
    static int  came_x[MAZE_H][MAZE_W];
    static int  came_y[MAZE_H][MAZE_W];
    int oc = 0, cc = 0;

    memset(came_x, -1, sizeof(came_x));
    memset(came_y, -1, sizeof(came_y));

    open[oc++] = (Node){px, py, -1, -1, 0, heuristic(px,py,gx,gy)};

    while (oc > 0) {
        /* pick lowest f */
        int bi = 0;
        for (int i = 1; i < oc; i++)
            if (open[i].f < open[bi].f) bi = i;
        Node cur = open[bi];
        open[bi] = open[--oc];

        if (cur.x == gx && cur.y == gy) {
            /* trace back to first step */
            int fx = cur.x, fy = cur.y;
            while (!(came_x[fy][fx] == px && came_y[fy][fx] == py)) {
                int nx = came_x[fy][fx], ny = came_y[fy][fx];
                if (nx < 0) break;
                fx = nx; fy = ny;
            }
            if      (fy == py - 1) return 'N';
            else if (fx == px + 1) return 'E';
            else if (fy == py + 1) return 'S';
            else if (fx == px - 1) return 'W';
            return '\0';
        }

        closed[cc++] = cur;

        int dx[] = {0,1,0,-1};
        int dy[] = {-1,0,1,0};
        uint8_t wf[] = {WALL_N, WALL_E, WALL_S, WALL_W};

        for (int d = 0; d < 4; d++) {
            if (maze[cur.y][cur.x].walls & wf[d]) continue;
            int nx = cur.x + dx[d], ny = cur.y + dy[d];
            if (nx < 0 || nx >= MAZE_W || ny < 0 || ny >= MAZE_H) continue;

            bool inClosed = false;
            for (int i = 0; i < cc; i++)
                if (closed[i].x == nx && closed[i].y == ny) { inClosed = true; break; }
            if (inClosed) continue;

            int ng = cur.g + 1;
            bool inOpen = false;
            for (int i = 0; i < oc; i++) {
                if (open[i].x == nx && open[i].y == ny) {
                    inOpen = true;
                    if (ng < open[i].g) {
                        open[i].g = ng;
                        open[i].f = ng + heuristic(nx,ny,gx,gy);
                        came_x[ny][nx] = cur.x;
                        came_y[ny][nx] = cur.y;
                    }
                    break;
                }
            }
            if (!inOpen && oc < MAZE_W*MAZE_H) {
                came_x[ny][nx] = cur.x;
                came_y[ny][nx] = cur.y;
                open[oc++] = (Node){nx, ny, cur.x, cur.y, ng, ng + heuristic(nx,ny,gx,gy)};
            }
        }
    }
    return '\0'; /* no path */
}

/* ── Helpers to build test mazes ── */

/* Fully walled maze — no passages anywhere */
static void make_closed_maze(Cell maze[][21]) {
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            maze[y][x] = (Cell){WALL_N|WALL_E|WALL_S|WALL_W, false};
}

/* Open corridor: carve east passages all the way across row 0, then south
   at the last column down to MAZE_H-1.  Player starts (0,0), goal (20,14). */
static void make_l_corridor(Cell maze[][21]) {
    make_closed_maze(maze);
    /* carve east along row 0 */
    for (int x = 0; x < MAZE_W - 1; x++) {
        maze[0][x].walls   &= ~WALL_E;
        maze[0][x+1].walls &= ~WALL_W;
    }
    /* carve south along last column */
    for (int y = 0; y < MAZE_H - 1; y++) {
        maze[y][MAZE_W-1].walls   &= ~WALL_S;
        maze[y+1][MAZE_W-1].walls &= ~WALL_N;
    }
}

/* Straight east corridor along row 0 only (can't reach goal at 20,14) */
static void make_dead_end(Cell maze[][21]) {
    make_closed_maze(maze);
    for (int x = 0; x < MAZE_W - 1; x++) {
        maze[0][x].walls   &= ~WALL_E;
        maze[0][x+1].walls &= ~WALL_W;
    }
    /* no south passage from (20,0) so goal (20,14) unreachable */
}

/* Fully open maze — every internal wall removed */
static void make_open_maze(Cell maze[][21]) {
    make_closed_maze(maze);
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            if (x < MAZE_W-1) { maze[y][x].walls &= ~WALL_E; maze[y][x+1].walls &= ~WALL_W; }
            if (y < MAZE_H-1) { maze[y][x].walls &= ~WALL_S; maze[y+1][x].walls &= ~WALL_N; }
        }
    }
}

static Cell g_maze[MAZE_H][MAZE_W];

/* ════════════════════════════════════════════════
 * SUITE 1 — A* Valid Direction  (TC-013)
 * ════════════════════════════════════════════════ */

void test_astar_returns_valid_direction(void) {
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT(m == 'N' || m == 'E' || m == 'S' || m == 'W');
}

void test_astar_not_null_char_on_solvable(void) {
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT(m != '\0');
}

void test_astar_first_step_east_on_l_corridor(void) {
    /* from (0,0) the only open passage is east */
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT_EQUAL(m, 'E');
}

void test_astar_open_maze_valid_direction(void) {
    make_open_maze(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT(m == 'N' || m == 'E' || m == 'S' || m == 'W');
}

/* ════════════════════════════════════════════════
 * SUITE 2 — A* Shortest Path  (TC-014)
 * ════════════════════════════════════════════════ */

void test_astar_shortest_path_corridor_start(void) {
    /* on the L corridor from (0,0) shortest path goes east first */
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT_EQUAL(m, 'E');
}

void test_astar_chooses_east_not_blocked_direction(void) {
    /* from midpoint of top row, should keep going east */
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, 5, 0);
    CU_ASSERT_EQUAL(m, 'E');
}

void test_astar_turns_south_at_corner(void) {
    /* at (MAZE_W-1, 0) the only remaining passage is south */
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, MAZE_W-1, 0);
    CU_ASSERT_EQUAL(m, 'S');
}

void test_astar_continues_south_mid_column(void) {
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, MAZE_W-1, 5);
    CU_ASSERT_EQUAL(m, 'S');
}

/* ════════════════════════════════════════════════
 * SUITE 3 — A* Edge Cases  (TC-015, TC-016)
 * ════════════════════════════════════════════════ */

void test_astar_at_goal_returns_null(void) {
    /* TC-015: already at goal → no move needed */
    make_l_corridor(g_maze);
    char m = astar_next_move_test(g_maze, MAZE_W-1, MAZE_H-1);
    CU_ASSERT_EQUAL(m, '\0');
}

void test_astar_unreachable_returns_null(void) {
    /* TC-016: fully walled maze → no path */
    make_closed_maze(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT_EQUAL(m, '\0');
}

void test_astar_dead_end_returns_null(void) {
    /* goal blocked: east corridor exists but south wall never carved */
    make_dead_end(g_maze);
    char m = astar_next_move_test(g_maze, 0, 0);
    CU_ASSERT_EQUAL(m, '\0');
}

void test_astar_no_crash_on_any_start(void) {
    /* just ensure no crash for arbitrary positions */
    make_open_maze(g_maze);
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            astar_next_move_test(g_maze, x, y); /* must not crash */
    CU_PASS("no crash");
}

/* ════════════════════════════════════════════════
 * SUITE 4 — Heuristic  (TC-022)
 * ════════════════════════════════════════════════ */

void test_heuristic_same_cell_is_zero(void) {
    CU_ASSERT_EQUAL(heuristic(5, 5, 5, 5), 0);
}

void test_heuristic_adjacent_is_one(void) {
    CU_ASSERT_EQUAL(heuristic(0, 0, 1, 0), 1);
    CU_ASSERT_EQUAL(heuristic(0, 0, 0, 1), 1);
}

void test_heuristic_is_manhattan_not_euclidean(void) {
    /* Manhattan(3,4) = 7, Euclidean = 5 */
    CU_ASSERT_EQUAL(heuristic(0, 0, 3, 4), 7);
}

void test_heuristic_is_symmetric(void) {
    CU_ASSERT_EQUAL(heuristic(2, 3, 8, 9), heuristic(8, 9, 2, 3));
}

void test_heuristic_never_negative(void) {
    CU_ASSERT(heuristic(0, 0, MAZE_W-1, MAZE_H-1) >= 0);
    CU_ASSERT(heuristic(MAZE_W-1, MAZE_H-1, 0, 0) >= 0);
}

void test_heuristic_goal_value(void) {
    /* from (0,0) to (20,14): Manhattan = 20+14 = 34 */
    CU_ASSERT_EQUAL(heuristic(0, 0, MAZE_W-1, MAZE_H-1), (MAZE_W-1)+(MAZE_H-1));
}

void test_heuristic_deterministic(void) {
    int h1 = heuristic(3, 7, 10, 2);
    int h2 = heuristic(3, 7, 10, 2);
    CU_ASSERT_EQUAL(h1, h2);
}

/* ════════════════════════════════════════════════
 * SUITE 5 — Telemetry JSON Fields  (TC-025, TC-026)
 * ════════════════════════════════════════════════ */

/* Inline the function exactly as in maze_sdl2.c */
static void create_player_move_json(
    const char *event_type, const char *device,
    int move_sequence, int pos_x, int pos_y,
    bool goal_reached, const char *timestamp,
    const char *move_dir, char *output_buffer, size_t buffer_size)
{
    snprintf(output_buffer, buffer_size,
        "{"
            "\"event_type\": \"%s\","
            "\"input\": {\"device\": \"%s\", \"move_sequence\": %d, \"move_dir\": \"%s\"},"
            "\"player\": {\"position\": {\"x\": %d, \"y\": %d}},"
            "\"goal_reached\": %s,"
            "\"timestamp\": \"%s\""
        "}",
        event_type, device, move_sequence, move_dir,
        pos_x, pos_y,
        goal_reached ? "true" : "false",
        timestamp);
}

#define JBUF 512
static char jbuf[JBUF];

static void build_sample_json(const char *dir) {
    create_player_move_json(
        "player_move", "joystick",
        1, 3, 7, false, "2026-04-17T12:00:00Z",
        dir, jbuf, JBUF);
}

void test_json_has_event_type(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"event_type\"") != NULL);
}

void test_json_has_input_key(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"input\"") != NULL);
}

void test_json_has_move_dir(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"move_dir\"") != NULL);
}

void test_json_has_position(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"position\"") != NULL);
}

void test_json_has_goal_reached(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"goal_reached\"") != NULL);
}

void test_json_has_timestamp(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"timestamp\"") != NULL);
}

void test_json_forward_dir(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"move_dir\": \"forward\"") != NULL);
}

void test_json_backward_dir(void) {
    build_sample_json("backward");
    CU_ASSERT(strstr(jbuf, "\"move_dir\": \"backward\"") != NULL);
}

void test_json_left_dir(void) {
    build_sample_json("left");
    CU_ASSERT(strstr(jbuf, "\"move_dir\": \"left\"") != NULL);
}

void test_json_right_dir(void) {
    build_sample_json("right");
    CU_ASSERT(strstr(jbuf, "\"move_dir\": \"right\"") != NULL);
}

void test_json_position_values(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"x\": 3") != NULL);
    CU_ASSERT(strstr(jbuf, "\"y\": 7") != NULL);
}

void test_json_goal_reached_false(void) {
    build_sample_json("forward");
    CU_ASSERT(strstr(jbuf, "\"goal_reached\": false") != NULL);
}

void test_json_goal_reached_true(void) {
    create_player_move_json(
        "player_move", "joystick",
        1, 20, 14, true, "2026-04-17T12:00:00Z",
        "forward", jbuf, JBUF);
    CU_ASSERT(strstr(jbuf, "\"goal_reached\": true") != NULL);
}

void test_json_is_valid_braces(void) {
    build_sample_json("forward");
    CU_ASSERT(jbuf[0] == '{');
    CU_ASSERT(jbuf[strlen(jbuf)-1] == '}');
}

/* TC-026: malformed / edge-case input */
void test_json_empty_dir_no_crash(void) {
    build_sample_json("");
    CU_ASSERT(jbuf[0] == '{'); /* still produces valid JSON shell */
}

void test_json_long_dir_truncated_safely(void) {
    char longdir[300];
    memset(longdir, 'x', 299);
    longdir[299] = '\0';
    create_player_move_json(
        "player_move", "joystick",
        1, 0, 0, false, "2026-04-17T12:00:00Z",
        longdir, jbuf, JBUF);
    CU_ASSERT(strlen(jbuf) < JBUF); /* must not overflow buffer */
}

void test_json_move_sequence_reflected(void) {
    create_player_move_json(
        "player_move", "joystick",
        42, 0, 0, false, "2026-04-17T12:00:00Z",
        "forward", jbuf, JBUF);
    CU_ASSERT(strstr(jbuf, "\"move_sequence\": 42") != NULL);
}

/* ════════════════════════════════════════════════
 * MAIN
 * ════════════════════════════════════════════════ */

int main(void) {
    CU_initialize_registry();

    /* Suite 1 — A* Valid Direction (TC-013) */
    CU_pSuite s1 = CU_add_suite("TC-013: A* Valid Direction", NULL, NULL);
    CU_add_test(s1, "returns N/E/S/W on solvable maze",    test_astar_returns_valid_direction);
    CU_add_test(s1, "not null char on solvable maze",      test_astar_not_null_char_on_solvable);
    CU_add_test(s1, "first step east on L-corridor",       test_astar_first_step_east_on_l_corridor);
    CU_add_test(s1, "valid direction on fully open maze",  test_astar_open_maze_valid_direction);

    /* Suite 2 — A* Shortest Path (TC-014) */
    CU_pSuite s2 = CU_add_suite("TC-014: A* Shortest Path", NULL, NULL);
    CU_add_test(s2, "first step east from (0,0)",          test_astar_shortest_path_corridor_start);
    CU_add_test(s2, "continues east mid corridor",         test_astar_chooses_east_not_blocked_direction);
    CU_add_test(s2, "turns south at top-right corner",     test_astar_turns_south_at_corner);
    CU_add_test(s2, "continues south mid column",          test_astar_continues_south_mid_column);

    /* Suite 3 — A* Edge Cases (TC-015, TC-016) */
    CU_pSuite s3 = CU_add_suite("TC-015/016: A* Edge Cases", NULL, NULL);
    CU_add_test(s3, "at goal returns null char",           test_astar_at_goal_returns_null);
    CU_add_test(s3, "fully walled returns null char",      test_astar_unreachable_returns_null);
    CU_add_test(s3, "dead-end maze returns null char",     test_astar_dead_end_returns_null);
    CU_add_test(s3, "no crash for any start position",     test_astar_no_crash_on_any_start);

    /* Suite 4 — Heuristic (TC-022) */
    CU_pSuite s4 = CU_add_suite("TC-022: Heuristic", NULL, NULL);
    CU_add_test(s4, "same cell = 0",                       test_heuristic_same_cell_is_zero);
    CU_add_test(s4, "adjacent cell = 1",                   test_heuristic_adjacent_is_one);
    CU_add_test(s4, "Manhattan not Euclidean",             test_heuristic_is_manhattan_not_euclidean);
    CU_add_test(s4, "symmetric",                           test_heuristic_is_symmetric);
    CU_add_test(s4, "never negative",                      test_heuristic_never_negative);
    CU_add_test(s4, "correct value (0,0)->(20,14) = 34",   test_heuristic_goal_value);
    CU_add_test(s4, "deterministic same inputs",           test_heuristic_deterministic);

    /* Suite 5 — Telemetry JSON (TC-025, TC-026) */
    CU_pSuite s5 = CU_add_suite("TC-025/026: Telemetry JSON", NULL, NULL);
    CU_add_test(s5, "has event_type field",                test_json_has_event_type);
    CU_add_test(s5, "has input field",                     test_json_has_input_key);
    CU_add_test(s5, "has move_dir field",                  test_json_has_move_dir);
    CU_add_test(s5, "has position field",                  test_json_has_position);
    CU_add_test(s5, "has goal_reached field",              test_json_has_goal_reached);
    CU_add_test(s5, "has timestamp field",                 test_json_has_timestamp);
    CU_add_test(s5, "forward direction in payload",        test_json_forward_dir);
    CU_add_test(s5, "backward direction in payload",       test_json_backward_dir);
    CU_add_test(s5, "left direction in payload",           test_json_left_dir);
    CU_add_test(s5, "right direction in payload",          test_json_right_dir);
    CU_add_test(s5, "x/y position values correct",        test_json_position_values);
    CU_add_test(s5, "goal_reached false",                  test_json_goal_reached_false);
    CU_add_test(s5, "goal_reached true",                   test_json_goal_reached_true);
    CU_add_test(s5, "JSON starts/ends with braces",        test_json_is_valid_braces);
    CU_add_test(s5, "empty dir no crash (TC-026)",         test_json_empty_dir_no_crash);
    CU_add_test(s5, "long dir truncated safely (TC-026)",  test_json_long_dir_truncated_safely);
    CU_add_test(s5, "move_sequence value reflected",       test_json_move_sequence_reflected);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    unsigned int failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;
}
