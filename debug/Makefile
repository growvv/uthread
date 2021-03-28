CC = gcc            #定义变量并赋值
CFLAGS =  -g -pthread -I .
SRC = ../uthread.c ../uthread_sched.c ../uthread_socket.c ../timer.c

all: myserver myclient

myserver: myserver.c
	$(CC) $(CFLAGS)  $(SRC) myserver.c -o  myserver

myclient: myclient.c
	$(CC) $(CFLAGS)  $(SRC) myclient.c -o  myclient

server_client: myclient.c
	$(CC) $(CFLAGS)  $(SRC) server_client.c -o  server_client

.PHONY: clean
clean:
	rm myserver myclient server_client
