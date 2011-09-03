#define _GNU_SOURCE
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <curl/curl.h>

#include "nosr.h"
#include "update.h"
#include "util.h"

static CURL *curl;

static struct repo_t *repo_new(const char *reponame)
{
	struct repo_t *repo;

	CALLOC(repo, 1, sizeof(struct repo_t), return NULL);

	if(asprintf(&repo->name, "%s", reponame) == -1) {
		free(repo);
		return NULL;
	}

	return repo;
}

void repo_free(struct repo_t *repo)
{
	size_t i;

	free(repo->name);
	for(i = 0; i < repo->servercount; i++) {
		free(repo->servers[i]);
	}
	free(repo->servers);

	free(repo);
}

static int repo_add_server(struct repo_t *repo, const char *server)
{
	if(!repo) {
		return 1;
	}

	repo->servers = realloc(repo->servers,
			sizeof(char *) * (repo->servercount + 1));

	repo->servers[repo->servercount] = strdup(server);
	repo->servercount++;

	return 0;
}

static int download(const char *urlbase, const char *repo)
{
	const char *basename;
	char *url = NULL;
	double bytes_dl;
	int ret = 1;
	FILE *fp = NULL;

	if(asprintf(&url, "%s/%s.files.tar.gz", urlbase, repo) == -1) {
		return 1;
	}

	basename = strrchr(url, '/');
	if(basename) {
		basename++;
	} else {
		goto cleanup;
	}

	fp = fopen(basename, "wb");
	if(!fp) {
		goto cleanup;
	}

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	printf("==> Downloading %s\n", basename);

	ret = curl_easy_perform(curl);

	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &bytes_dl);
	if(bytes_dl <= 0) {
		unlink(basename);
	}

	if(ret != CURLE_OK) {
		printf("warning: failed to download %s\n", url);
	}

cleanup:
	free(url);
	fclose(fp);

	return !(ret == CURLE_OK);
}

static char *prepare_url(const char *url, const char *repo, const char *arch)
{
	char *string, *temp = NULL;
	const char * const archvar = "$arch";
	const char * const repovar = "$repo";

	string = strdup(url);
	temp = string;
	if(strstr(temp, archvar)) {
		string = strreplace(temp, archvar, arch);
		free(temp);
		temp = string;
	}

	if(strstr(temp, repovar)) {
		string = strreplace(temp, repovar, repo);
		free(temp);
		temp = string;
	}

	return string;
}

static char *line_get_val(char *line, const char *sep)
{
	strsep(&line, sep);
	strtrim(line);
	return line;
}

static int add_servers_from_include(struct repo_t *repo, char *file)
{
	char *ptr;
	char line[4096];
	const char * const server = "Server";
	FILE *fp;

	fp = fopen(file, "r");
	if(!fp) {
		perror("fopen");
		return 1;
	}

	while(fgets(line, 4096, fp)) {
		if((ptr = strchr(line, '#'))) {
			*ptr = '\0';
		}
		strtrim(line);
		if(!strlen(line)) {
			continue;
		}

		if(strncmp(line, server, strlen(server)) == 0) {
			ptr = line_get_val(line, "=");
			repo_add_server(repo, ptr);
		}
	}

	fclose(fp);

	return 0;
}

struct repo_t **find_active_repos(const char *filename, int *repocount)
{
	FILE *fp;
	char *ptr, *section = NULL;
	char line[4096];
	const char * const server = "Server";
	const char * const include = "Include";
	struct repo_t **active_repos = NULL;
	int in_options = 0;

	*repocount = 0;

	fp = fopen(filename, "r");
	if(!fp) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	while(fgets(line, 4096, fp)) {
		if((ptr = strchr(line, '#'))) {
			*ptr = '\0';
		}
		strtrim(line);
		if(!strlen(line)) {
			continue;
		}

		if (line[0] == '[' && line[strlen(line) - 1] == ']') {
			free(section);
			section = strndup(&line[1], strlen(line) - 2);
			if(strcmp(section, "options") == 0) {
				in_options = 1;
				continue;
			} else {
				in_options = 0;
				active_repos = realloc(active_repos, sizeof(struct repo_t *) * (*repocount + 1));
				active_repos[*repocount] = repo_new(section);
				(*repocount)++;
			}
		}

		if(in_options) {
			continue;
		}

		if(strchr(line, '=')) {
			char *key = line, *val = line_get_val(line, "=");
			strtrim(key);

			if(strcmp(key, server) == 0) {
				repo_add_server(active_repos[*repocount - 1], val);
			} else if(strcmp(key, include) == 0) {
				add_servers_from_include(active_repos[*repocount - 1], val);
			}
		}
	}

	free(section);
	fclose(fp);

	return active_repos;
}

static int download_repo_files(struct repo_t *repo)
{
	char *url;
	size_t i;
	int ret;
	struct utsname un;

	uname(&un);

	for(i = 0; i < repo->servercount; i++) {
		url = prepare_url(repo->servers[i], repo->name, un.machine);
		ret = download(url, repo->name);
		free(url);
		if(ret == 0) {
			return 0;
		}
	}

	return 1;
}

int nosr_update(struct repo_t **repos)
{
	struct repo_t **repo;
	int ret = 0;

	if(access(DBPATH, W_OK)) {
		fprintf(stderr, "error: unable to write to %s: ", DBPATH);
		perror("");
		return 1;
	}

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

	for(repo = repos; *repo; repo++) {
		ret += download_repo_files(*repo);
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
