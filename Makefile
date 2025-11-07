WARNINGS=-Wnull-dereference -Wall -Winline -Wextra -Wno-unused-parameter
CFLAGS+=-g -std=gnu11 $(WARNINGS) -I$(CURDIR) -I$(CURDIR)/vendor
LDLIBS+=-lm -lreadline vendor/utf8proc/libutf8proc.a
SANITIZERS+=address,undefined

ifeq ($(TARGET),release)
	CFLAGS+=-DNDEBUG -O3 -flto -fomit-frame-pointer -fno-asynchronous-unwind-tables -march=native
else ifeq ($(TARGET),profile)
	CFLAGS+=-DPROFILE -pg -O3 -flto --coverage
endif

ifneq ($(TARGET),release)
ifneq ($(TARGET),profile)
	CFLAGS+=-fsanitize=$(SANITIZERS)
endif
	CFLAGS+=-fno-omit-frame-pointer
endif

ifeq ($(TARGET),debug)
	CFLAGS+=-DCB_DEBUG_VM
endif

SRC = $(wildcard *.c modules/*.c)
OBJ := $(patsubst %.c,%.o,$(SRC))
DEP := $(patsubst %.o,%.d,$(OBJ))

cbcvm: $(OBJ) vendor/utf8proc/libutf8proc.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

-include $(DEP)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

vendor/utf8proc/libutf8proc.a:
	$(MAKE) -C vendor/utf8proc

# Generate a release binary using profile-guided optimization
profile-opt:
	CFLAGS='-fprofile-instr-generate' $(MAKE) clean cbcvm TARGET=release
	LLVM_PROFILE_FILE='default_%p_%m.profraw' ./cbcvm bench/bf.cb bench/bench.b
	llvm-profdata merge -o default.profdata default_*.profraw
	rm cbcvm
	CFLAGS='-fprofile-instr-use=default.profdata' $(MAKE) TARGET=release
	find . -name '*.gcda' -delete
	rm -f default_*.profraw default.profdata

clean:
	[ -f cbcvm ] && rm cbcvm || true
	-rm -f $(OBJ) $(DEP)
	find . \( -name '*.gcda' -o -name '*.gcno' -o -name 'gmon.out' \) -delete
	$(MAKE) -C vendor/utf8proc clean

.PHONY: clean profile-opt