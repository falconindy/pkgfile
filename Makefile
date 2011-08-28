OUT        = nosr
VERSION    = 0.1
CPPFLAGS  := -DVERSION=\"$(VERSION)\" $(CPPFLAGS)
CFLAGS    := --std=c99 -g -pedantic -Wall -Wextra -Werror -pthread $(CFLAGS) $(CPPFLAGS)
LDFLAGS   := -larchive -lpcre -lcurl $(LDFLAGS)

PREFIX    ?= /usr/local

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(OUT) doc

.c.o:
	$(CC) -c $(CFLAGS) $<

doc: nosr.1
nosr.1: README.pod
	pod2man --section=1 --center="Nosr Manual" --name="NOSR" --release="nosr $(VERSION)" $< > $@

$(OUT): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: all
	install -Dm755 nosr $(DESTDIR)$(PREFIX)/bin/nosr
	install -Dm644 nosr.1 $(DESTDIR)$(PREFIX)/share/man/man1/nosr.1
	install -d $(DESTDIR)/var/cache/nosr

strip: $(OUT)
	strip --strip-all $(OUT)

clean:
	$(RM) $(OBJ) $(OUT) nosr.1
