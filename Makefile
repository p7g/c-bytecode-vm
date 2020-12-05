CFLAGS+=-g -std=gnu11 -Wall
LDFLAGS+=-lm

ifeq ($(TARGET),release)
	CFLAGS+=-DNDEBUG -O3 -flto
else ifeq ($(TARGET),debug)
	CFLAGS+=-DDEBUG_DISASM -DDEBUG_VM -DDEBUG_GC
else ifeq ($(TARGET),profile)
	CFLAGS+=-DPROFILE -pg -O3 -flto --coverage
endif

cbcvm: *.c *.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o cbcvm *.c

# Generate a release binary using profile-guided optimization
profile-opt:
	CFLAGS='-fprofile-generate' $(MAKE) TARGET=release
	./cbcvm bf.rbcvm bench.b
	$(MAKE) clean
	CFLAGS='-fprofile-use -fprofile-correction' $(MAKE) TARGET=release
	find . -name '*.gcda' -delete

clean:
	rm cbcvm

.PHONY: clean profile-opt
