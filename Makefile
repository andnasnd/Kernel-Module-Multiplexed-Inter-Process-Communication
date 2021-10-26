all:lab3server lab3client

CFLAGS=-g -O0 -Wall

lab3server:lab3server.o lab3avl.o
	$(CC) -o $@ lab3server.o lab3avl.o
	
lab3client:lab3client.o lab3avl.o
	$(CC) -o $@ lab3client.o lab3avl.o 

lab3avl.o:lab3avl.h lab3avl.c

lab3server.o:lab3server.c
lab3client.o:lab3client.c

.PHONY:clean

clean:
	rm *.o 
