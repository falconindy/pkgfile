#pragma once

#define PACMANCONFIG "/etc/pacman.conf"

struct config_t;
class AlpmConfig;

int pkgfile_update(AlpmConfig* alpm_config, struct config_t* config);

/* vim: set ts=2 sw=2 et: */
