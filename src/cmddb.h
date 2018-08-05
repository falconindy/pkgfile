#pragma once

void cmddb_gen(struct repo_t *repo, struct config_t *config);
void cmddb_genall(struct repovec_t *repos, struct config_t *config);
bool cmddb_valid(const char *reponame);
struct result_t *cmddb_search(struct result_t *result, const char *reponame, const char *cmd);
struct result_t **cmddb_search_all(struct repovec_t *repos, const char *cmd);
