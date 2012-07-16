OUT        = pkgfile
VERSION    = 1
CPPFLAGS  := -DVERSION=\"v$(VERSION)\" -D_FILE_OFFSET_BITS=64 $(CPPFLAGS)
CFLAGS    := -std=c99 -g -pedantic -pthread -Wall -Wextra $(CFLAGS) $(CPPFLAGS)
LDFLAGS   := -pthread -larchive -lpcre -lcurl $(LDFLAGS)

PREFIX    ?= /usr/local

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(OUT) doc
$(OUT): $(OBJ)

doc: pkgfile.1
pkgfile.1: README.pod
	pod2man --section=1 --center="pkgfile Manual" --name="pkgfile" --release="pkgfile $(VERSION)" $< > $@

$(OUT): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: all
	install -Dm755 pkgfile $(DESTDIR)$(PREFIX)/bin/pkgfile
	ln -s pkgfile $(DESTDIR)$(PREFIX)/bin/nosr
	install -Dm644 pkgfile.1 $(DESTDIR)$(PREFIX)/share/man/man1/pkgfile.1
	install -dm775 $(DESTDIR)/var/cache/pkgfile
	install -Dm644 bash_completion $(DESTDIR)/usr/share/bash-completion/completions/pkgfile

dist:
	git archive --format=tar --prefix=$(OUT)-$(VERSION)/ v$(VERSION) | gzip -9 > $(OUT)-$(VERSION).tar.gz

strip: $(OUT)
	strip --strip-all $(OUT)

clean:
	$(RM) $(OBJ) $(OUT) pkgfile.1
