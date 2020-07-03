CFLAGS+=-g -std=gnu11 -Wall
LDFLAGS+=-lm

ifeq ($(TARGET),release)
	CFLAGS+=-DNDEBUG -O3 -flto
else ifeq ($(TARGET),debug)
	CFLAGS+=-DDEBUG_DISASM -Og
endif

cbcvm: *.c *.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o cbcvm *.c

clean:
	rm cbcvm

.PHONY: clean
