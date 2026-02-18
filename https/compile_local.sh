gcc -O2 -Wall -Wextra -std=c11 maze_https_mongo.c -o maze_https_mongo   $(pkg-config --cflags --libs libmicrohttpd libmongoc-1.0 gnutls)
./maze_https_mongo