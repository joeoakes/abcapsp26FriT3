gcc maze_sdl2.c -o maze_sdl2 `sdl2-config --cflags --libs` -lcurl $(pkg-config --cflags --libs libmongoc-1.0)
./maze_sdl2