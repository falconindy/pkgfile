ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory

bashcompletiondir=$(datarootdir)/bash-completion/completions
zshcompletiondir=$(datarootdir)/zsh/site-functions
shelldatadir=$(datarootdir)/doc/pkgfile

dist_shelldata_DATA = \
	extra/command-not-found.bash \
	extra/command-not-found.zsh

EXTRA_DIST = \
	README.pod \
	extra/bash-completion \
	extra/zsh-completion

dist_man_MANS = \
	pkgfile.1

bin_PROGRAMS = \
	pkgfile

if HAVE_SYSTEMD
dist_systemdsystemunit_DATA = \
	systemd/pkgfile-update.service \
	systemd/pkgfile-update.timer
endif

if USE_GIT_VERSION
GIT_VERSION := $(shell git describe --abbrev=4 --dirty | sed 's/^v//')
REAL_PACKAGE_VERSION = $(GIT_VERSION)
else
REAL_PACKAGE_VERSION = $(PACKAGE_VERSION)
endif

AM_CFLAGS = \
	-pthread \
	$(WARNINGFLAGS)

AM_LDFLAGS = \
	-pthread \
	$(GCLDFLAGS)

AM_CPPFLAGS = \
	-imacros $(top_builddir)/config.h \
	-I${top_srcdir}/src

if USE_GIT_VERSION
AM_CPPFLAGS += \
	-DGIT_VERSION=\"$(GIT_VERSION)\"
endif

pkgfile_SOURCES = \
	src/match.c src/match.h \
	src/pkgfile.c src/pkgfile.h \
	src/repo.c src/repo.h \
	src/result.c src/result.h \
	src/update.c src/update.h \
	src/macro.h src/missing.h

pkgfile_CFLAGS = \
	$(AM_CFLAGS) \
	$(ARCHIVE_CFLAGS) \
	$(CURL_CFLAGS) \
	$(PCRE_CFLAGS) \
	$(TCMALLOC_CFLAGS)

pkgfile_LDADD = \
	$(ARCHIVE_LIBS) \
	$(CURL_LIBS) \
	$(PCRE_LIBS) \
	$(TCMALLOC_LIBS)
# tcmalloc must be last to avoid heapchecker issues

pkgfile.1: README.pod
	$(AM_V_GEN)$(POD2MAN) \
		--section=1 \
		--center="pkgfile Manual" \
		--name="PKGFILE" \
		--release="pkgfile $(REAL_PACKAGE_VERSION)" $< > $@

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(bashcompletiondir)
	$(INSTALL_DATA) $(top_srcdir)/extra/bash-completion $(DESTDIR)$(bashcompletiondir)/pkgfile
	$(MKDIR_P) $(DESTDIR)$(zshcompletiondir)
	$(INSTALL_DATA) $(top_srcdir)/extra/zsh-completion $(DESTDIR)$(zshcompletiondir)/_pkgfile
	$(MKDIR_P) $(DESTDIR)$(CACHEPATH)
	chmod 775 $(DESTDIR)$(CACHEPATH)

uninstall-local:
	$(RM) $(DESTDIR)$(bashcompletiondir)/pkgfile
	$(RM) $(DESTDIR)$(zshcompletiondir)/_pkgfile

CLEANFILES = \
	$(dist_man_MANS)

DISTCHECK_CONFIGURE_FLAGS = \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir) \
	--with-cachedir=$$dc_install_base$(CACHEPATH)

# custom targets
upload: all distcheck
	gpg --detach-sign pkgfile-$(VERSION).tar.xz
	scp pkgfile-$(VERSION).tar.xz pkgfile-$(VERSION).tar.xz.sig code.falconindy.com:archive/pkgfile/

fmt:
	clang-format -i -style=Google $(pkgfile_SOURCES)
