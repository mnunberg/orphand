all: orphand

orphand: orphand.c hashtable.c
	$(CC) -I. -Wall -ggdb3 -o $@ $^
