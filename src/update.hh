#pragma once

#include <curl/curl.h>

#define PACMANCONFIG "/etc/pacman.conf"

struct config_t;
struct repovec_t;

int pkgfile_update(struct repovec_t *repos, struct config_t *config);

/* vim: set ts=2 sw=2 et: */
