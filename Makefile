CFLAGS = -O3 -g -std=gnu99 -Wall -pedantic

cbcvm: *.c *.h
	$(CC) $(CFLAGS) -o cbcvm *.c
