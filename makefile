CFLAGS = -g -std=gnu11 -Wall -Wextra
BIN = server client

all:$(BIN)

# val:
# 	valgrind ./server

client:client.o common.h common.o send_packet.o
	gcc $(CFLAGS) client.o common.o send_packet.o -o client

server:server.o common.h common.o send_packet.o
	gcc $(CFLAGS) server.o common.o send_packet.o -o server

client.o:client.c
	gcc $(CFLAGS) -c client.c

server.o:server.c
	gcc $(CFLAGS) -c server.c

common.o:common.c
	gcc $(CFLAGS) -c common.c

send_packet.o:send_packet.c
	gcc $(CFLAGS) -c send_packet.c

clean:
	rm -f $(BIN) *.o kernel-file*

kernel:
	rm -f kernel-file*
