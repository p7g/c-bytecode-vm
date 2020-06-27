CFLAGS=-O3 -g -std=gnu11 -Wall

cbcvm: *.c *.h
	$(CC) $(CFLAGS) -o cbcvm *.c
