CFLAGS+=-g -std=gnu11 -Wall -I$(CURDIR)
LDFLAGS+=-lm -lreadline
SANITIZERS+=address,undefined

ifeq ($(TARGET),release)
	CFLAGS+=-DNDEBUG -O3 -flto
else ifeq ($(TARGET),profile)
	CFLAGS+=-DPROFILE -pg -O3 -flto --coverage
endif

ifneq ($(TARGET),release)
	CFLAGS+=-fsanitize=$(SANITIZERS) -fno-omit-frame-pointer
endif

cbcvm: *.c *.h modules/*.c modules/*.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o cbcvm *.c modules/*.c

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
