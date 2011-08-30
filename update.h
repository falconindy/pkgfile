#ifndef _NOSR_UPDATE_H
#define _NOSR_UPDATE_H

#define PACMANCONFIG "/etc/pacman.conf"

struct repo_t {
	char *name;
	char **servers;
	size_t servercount;
};

struct repo_t **find_active_repos(const char *filename);
int nosr_update(struct repo_t **repos);
struct repo_t *repo_new(const char *reponame);
int repo_add_server(struct repo_t *repo, const char *server);
void repo_free(struct repo_t *repo);

#endif /* _NOSR_UPDATE_H */

/* vim: set ts=2 sw=2 noet: */
