gcc -g -O0 -o maze_sdl2 maze_sdl2.c astar.c maze_learning.c $(sdl2-config --cflags --libs) $(pkg-config --cflags --libs libbson-1.0 libmongoc-1.0 hiredis libcurl) -lm
./maze_sdl2