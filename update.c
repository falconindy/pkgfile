#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include "util.h"

#define PACMANCONFIG "/etc/pacman.conf"
#define DBPATH       "/var/lib/pacman/sync"

static CURL *curl;

static int download(const char *urlbase, const char *repo)
{
	const char *basename;
	char *url;
	double bytes_dl;
	int ret;
	FILE *fp;

	if(asprintf(&url, "%s/%s.files.tar.gz", urlbase, repo) == -1) {
		return 1;
	}

	basename = strrchr(url, '/');
	if(basename) {
		basename++;
	} else {
		return 1;
	}

	fp = fopen(basename, "wb");
	if(!fp) {
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	printf("==> Downloading %s... ", basename);
	fflush(stdout);

	ret = curl_easy_perform(curl);

	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &bytes_dl);
	if(!bytes_dl) {
		unlink(basename);
	}

	fclose(fp);
	free(url);

	switch(ret) {
		case CURLE_OK:
			printf("done\n");
			break;
		default:
			printf("fail\n");
	}

	return !(ret == CURLE_OK);
}

static char *prepare_url(char *url, const char *repo)
{
	char *string, *temp = NULL;
	const char * const archvar = "$arch";
	const char * const repovar = "$repo";

	string = strdup(url);
	temp = string;
	if(strstr(temp, archvar)) {
		string = strreplace(temp, archvar, "x86_64");
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

static int parse_server_line(const char *repo, char *server)
{
	char *url;
	int ret;

	url = prepare_url(server, repo);
	if(!url) {
		return 1;
	}

	ret = download(url, repo);
	free(url);

	return ret;
}

static int parse_include_file(const char *repo, char *file)
{
	char *ptr, *url;
	char line[4096];
	const char * const server = "Server";
	int ret = 1;
	FILE *fp;

	fp = fopen(file, "r");
	if(!fp) {
		perror("fopen");
		return 1;
	}

	while(fgets(line, 4096, fp)) {
		strtrim(line);
		if (!strlen(line) || line[0] == '#') {
			continue;
		}
		if ((ptr = strchr(line, '#'))) {
			*ptr = '\0';
			strtrim(line);
		}

		if(strncmp(line, server, strlen(server)) == 0) {
			ptr = line_get_val(line, "=");
			url = prepare_url(ptr, repo);
			ret = download(url, repo);
			free(url);
			if(ret == 0) {
				break;
			}
		}
	}

	fclose(fp);

	return ret;
}

static int do_update(const char *filename)
{
	FILE *fp;
	char *section = NULL, *ptr;
	char line[4096];
	const char * const server = "Server";
	const char * const include = "Include";
	int section_done = 0;

	fp = fopen(filename, "r");
	if(!fp) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return 1;
	}

	while(fgets(line, 4096, fp)) {
		strtrim(line);

		if (!strlen(line) || line[0] == '#') {
			continue;
		}
		if ((ptr = strchr(line, '#'))) {
			*ptr = '\0';
			strtrim(line);
		}

		if (line[0] == '[' && line[strlen(line) - 1] == ']') {
			free(section);
			section_done = 0;
			section = strndup(&line[1], strlen(line) - 2);
		}

		if(section_done || strcmp(section, "options") == 0) {
			continue;
		}

		if(strchr(line, '=')) {
			char *key = line;
			char *val = line_get_val(line, "=");
			strtrim(key);

			if(strcmp(key, server) == 0) {
				if(parse_server_line(section, val) == 0) {
					section_done = 1;
				}
			} else if(strcmp(line, include) == 0) {
				if(parse_include_file(section, val) == 0) {
					section_done = 1;
				}
			}
		}
	}

	free(section);
	fclose(fp);

	return 0;
}

int nosr_update()
{
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

	do_update(PACMANCONFIG);

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	return 0;
}

/* vim: set ts=2 sw=2 noet: */
