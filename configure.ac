AC_PREREQ(2.60)
AC_INIT(pkgfile,
	[3],
	[dreisner@archlinux.org])

AC_CONFIG_SRCDIR([src/pkgfile.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([/usr])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_PROG_SED
AC_PROG_MKDIR_P
AC_CHECK_PROGS([POD2MAN], [pod2man])

AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects])
AM_SILENT_RULES([yes])

PKG_CHECK_MODULES(PCRE,    [ libpcre ])
PKG_CHECK_MODULES(ARCHIVE, [ libarchive >= 3.0.0 ])
PKG_CHECK_MODULES(CURL,    [ libcurl >= 7.19.4 ])

# Help line for using git version in pacman version string
AC_ARG_ENABLE(git-version,
	AS_HELP_STRING([--disable-git-version],
		[enable use of git version in version string if available]),
	[wantgitver=$enableval], [wantgitver=check])

AC_ARG_WITH(cachedir,
	AS_HELP_STRING([--with-cachedir=path],
		[location of .files cachedir]),
	[CACHEPATH=$withval], [CACHEPATH=/var/cache/pkgfile])
AC_DEFINE_UNQUOTED([CACHEPATH], "$CACHEPATH", [Path to .files cachedir])
AC_SUBST([CACHEPATH])

# Enable or disable use of git version in pacman version string
AC_MSG_CHECKING(whether to use git version if available)
if test "x$wantgitver" != "xno" ; then
	AC_CHECK_PROGS([GIT], [git])
	AC_CHECK_FILE([.git/], hasgitdir=yes)
	if test $GIT -a "x$hasgitdir" = "xyes"; then
		usegitver=yes
		AC_DEFINE([USE_GIT_VERSION], , [Use GIT version in version string])
	else
		usegitver=no
	fi
else
	AC_MSG_RESULT([no, disabled by configure])
	usegitver=no
fi
AM_CONDITIONAL(USE_GIT_VERSION, test "x$usegitver" = "xyes")

CC_CHECK_FLAGS_APPEND(with_cflags, [CFLAGS], [\
	-g \
	-pipe \
	-Wall \
	-W \
	-Wextra \
	-Wno-inline \
	-Wvla \
	-Wundef \
	-Wformat=2 \
	-Wlogical-op \
	-Wsign-compare \
	-Wformat-security \
	-Wmissing-include-dirs \
	-Wformat-nonliteral \
	-Wold-style-definition \
	-Wpointer-arith \
	-Winit-self \
	-Wdeclaration-after-statement \
	-Wfloat-equal \
	-Wmissing-prototypes \
	-Wstrict-prototypes \
	-Wredundant-decls \
	-Wmissing-declarations \
	-Wmissing-noreturn \
	-Wshadow \
	-Wendif-labels \
	-Wstrict-aliasing=2 \
	-Wwrite-strings \
	-Wno-long-long \
	-Wno-overlength-strings \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-unused-result \
	-Wnested-externs \
	-Wchar-subscripts \
	-Wtype-limits \
	-Wuninitialized \
	-fno-common \
	-fdiagnostics-show-option \
	-fvisibility=hidden \
	-ffunction-sections \
	-fdata-sections])
AC_SUBST([WARNINGFLAGS], $with_cflags)

CC_CHECK_FLAGS_APPEND([with_ldflags], [LDFLAGS], [ \
	-Wl,--as-needed])
AC_SUBST([GCLDFLAGS], $with_ldflags)

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([
	Makefile
])

AC_OUTPUT
AC_MSG_RESULT([
	prefix:                 $(eval echo ${prefix})
	datarootdir:            $(eval echo ${datarootdir})
	cache directory:        ${CACHEPATH}

	using git version:      ${usegitver}

	compiler:               ${CC}
	cflags:                 ${with_cflags} ${CFLAGS}
	ldflags:                ${with_ldflags} ${LDFLAGS}
])
