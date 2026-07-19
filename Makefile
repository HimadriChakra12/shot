CC     = cc
CFLAGS = -O2 -Wall -Wextra -std=c99 -Isrc
LIBS   = -lX11 -lwebp -lpng -ljpeg
# CFLAGS += -DDEBUG

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
BINARY  = shot

SRCS = main.c \
       src/capture.c \
       src/save.c \
       src/scripts.c \
       src/select.c \
       src/xutil.c

OBJS = $(SRCS:.c=.o)

all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BINARY)
	install -Dm755 $(BINARY) $(DESTDIR)$(BINDIR)/$(BINARY)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BINARY)

clean:
	rm -f $(OBJS) $(BINARY)

.PHONY: all install uninstall clean
