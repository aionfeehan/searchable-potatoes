all: server client #common

server: server.c
		$(CC) server.c -o server -Wall -Wextra -pedantic -std=c99 -DEBUG -g

client: client.c
		$(CC) client.c -o client -Wall -Wextra -pedantic -std=c99 -DEBUG -g

# common: common.c
# 		$(CC) common.c -o common -Wall -Wextra -pedantic -std=c99 -DEBUG -g