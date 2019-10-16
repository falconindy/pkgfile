## pkgfile

pkgfile answers the question of "what package owns this file?" or "what are the
file contents of this package?" even if the package isn't installed. pkgfile is
targetted at Arch Linux users and depends on the `.files` databases served by
package mirrors.

pkgfile differs from the functionality of `pacman -F` in that it's more
flexible (offering more ways to filter searches), has more friendly output for
machine consumption, and is generally much faster (generally by an order of
magnitude when reading from page cache).

This repo contains the source code for two binaries:

* `pkgfile`: the main commandline tool, used to search and list packages.
* `pkgfiled`: a daemon for keeping pkgfile's databases up to date with pacman.
