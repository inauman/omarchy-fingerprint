# Builds the offline evaluation tool only. The driver is built inside
# libfprint by build.sh.
CFLAGS ?= -O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

elanpress-eval: eval.c elanpress-match.c elanpress-match.h
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -o $@ eval.c elanpress-match.c $(GLIB_LIBS) -lm

clean:
	rm -f elanpress-eval
