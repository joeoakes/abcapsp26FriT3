gcc -O2 -Wall -Wextra -std=c11 maze_https_redis.c -o maze_https_redis   $(pkg-config --cflags --libs libmicrohttpd libmongoc-1.0 gnutls hiredis)
./maze_https_redis