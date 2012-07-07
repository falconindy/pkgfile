/*
 * Copyright (C) 2011-2012 by Dave Reisner <dreisner@archlinux.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <curl/curl.h>

#include "nosr.h"
#include "update.h"
#include "util.h"

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

	if(asprintf(&temp, "%s/%s.files", string, repo) == -1) {
		fprintf(stderr, "error: failed to allocate memory\n");
	}

	free(string);

	return temp;
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
		if(*strtrim(line) == '\0') {
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
		if(*strtrim(line) == '\0') {
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

static int decompress_repo_data(struct repo_t *repo)
{
	char diskfile[PATH_MAX], tmpfile[PATH_MAX];
	int ret = -1;
	struct archive *tarball, *cpio;
	struct archive_entry *ae;

	/* generally, repo files are gzip compressed, but there's no guarantee of
	 * this. in order to be compression-agnostic, use libarchive's reader/writer
	 * methods. this also gives us an opportunity to rewrite the archive as CPIO,
	 * which is marginally faster given our staunch sequential access. */

	snprintf(tmpfile, PATH_MAX, CACHEPATH "/%s.files~", repo->name);
	snprintf(diskfile, PATH_MAX, CACHEPATH "/%s.files", repo->name);

	tarball = archive_read_new();
	cpio = archive_write_new();

	if(tarball == NULL || cpio == NULL) {
		fprintf(stderr, "failed to allocate memory for archive objects\n");
		return -1;
	}

	archive_read_support_format_tar(tarball);
	archive_read_support_compression_all(tarball);
	ret = archive_read_open_memory(tarball, repo->data, repo->buflen);
	if (ret != ARCHIVE_OK) {
		fprintf(stderr, "failed to create archive reader for %s: %s\n",
				repo->name, archive_error_string(tarball));
		goto done;
	}

	archive_write_set_format_cpio(cpio);
	archive_write_add_filter_none(cpio);
	ret = archive_write_open_filename(cpio, tmpfile);
	if (ret != ARCHIVE_OK) {
		fprintf(stderr, "failed to open file for writing: %s: %s\n",
				tmpfile, archive_error_string(cpio));
		goto done;
	}

	while(archive_read_next_header(tarball, &ae) == ARCHIVE_OK) {
		unsigned char buf[BUFSIZ];
		int done = 0;
		if(archive_write_header(cpio, ae) != ARCHIVE_OK) {
			fprintf(stderr, "failed to write cpio header: %s\n",
					archive_error_string(cpio));
			break;
		}
		for(;;) {
			int bytes_r = archive_read_data(tarball, buf, sizeof(buf));
			if(bytes_r == 0) {
				break;
			}

			if(archive_write_data(cpio, buf, bytes_r) != bytes_r) {
				fprintf(stderr, "failed to write %d bytes to new files db: %s\n",
						bytes_r, archive_error_string(cpio));
				done = 1;
				break;
			}
		}
		if(done) {
			break;
		}
	}

	archive_read_close(tarball);
	archive_write_close(cpio);
	ret = 0;

done:
	archive_read_free(tarball);
	archive_write_free(cpio);

	if(ret == 0) {
		if(rename(tmpfile, diskfile) != 0) {
			fprintf(stderr, "failed to rotate new repo for %s into replace: %s\n",
					repo->name, strerror(errno));
		}
	} else {
		/* oh noes! */
		unlink(tmpfile);
	}

	return ret;
}

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *data)
{
	void *newdata;
	const size_t realsize = size * nmemb;
	struct repo_t *repo = (struct repo_t*)data;

	newdata = realloc(repo->data, repo->buflen + realsize + 1);
	if(!newdata) {
		fprintf(stderr, "error: failed to reallocate %zd bytes\n",
				repo->buflen + realsize + 1);
		return 0;
	}

	repo->data = newdata;
	memcpy(&(repo->data[repo->buflen]), ptr, realsize);
	repo->buflen += realsize;
	repo->data[repo->buflen] = '\0';

	return realsize;
}

static int add_repo_download(CURLM *multi, struct repo_t *repo)
{
	struct stat st;

	if(repo->curl == NULL) {
		/* it's my first time, be gentle */
		if(repo->servercount == 0) {
			fprintf(stderr, "error: no servers configured for repo %s\n", repo->name);
			return -1;
		}
		repo->curl = curl_easy_init();
		snprintf(repo->diskfile, sizeof(repo->diskfile), CACHEPATH "/%s.files", repo->name);
		curl_easy_setopt(repo->curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(repo->curl, CURLOPT_WRITEFUNCTION, write_response);
		curl_easy_setopt(repo->curl, CURLOPT_WRITEDATA, repo);
		curl_easy_setopt(repo->curl, CURLOPT_PRIVATE, repo);
		curl_easy_setopt(repo->curl, CURLOPT_ERRORBUFFER, repo->errmsg);
	} else {
		curl_multi_remove_handle(multi, repo->curl);
		FREE(repo->url);
		repo->server_idx++;
	}

	if(repo->server_idx >= repo->servercount) {
		fprintf(stderr, "error: failed to update repo: %s\n", repo->name);
		return -1;
	}

	repo->url = prepare_url(repo->servers[repo->server_idx], repo->name, repo->arch);
	if(repo->url == NULL) {
		fprintf(stderr, "error: failed to allocate URL for download\n");
		return -1;
	}

	curl_easy_setopt(repo->curl, CURLOPT_URL, repo->url);

	if(stat(repo->diskfile, &st) == 0) {
		curl_easy_setopt(repo->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
		curl_easy_setopt(repo->curl, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
	}

	curl_multi_add_handle(multi, repo->curl);

	return 0;
}

static int read_multi_msgs(CURLM *multi, int remaining)
{
	struct repo_t *repo;
	int msgs_left;
	CURLMsg *msg;

	msg = curl_multi_info_read(multi, &msgs_left);
	if(msg == NULL) {
		/* signal to the caller that we're out of messages */
		return -ENOENT;
	}

	curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (void *)&repo);
	if(msg->msg == CURLMSG_DONE) {
		long timecond, resp;

		curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &timecond);
		curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);

		if(timecond == 1) {
			printf("  %s is up to date\n", repo->name);
			repo->err = 0;
			return 0;
		}

		/* was it a success? */
		if(msg->data.result != CURLE_OK || resp >= 400) {
			if(*repo->errmsg) {
				fprintf(stderr, "warning: download failed: %s: %s\n", repo->url,
						repo->errmsg);
			} else {
				fprintf(stderr, "warning: download failed: %s [HTTP %ld]\n",
						repo->url, resp);
			}

			add_repo_download(multi, repo);
			return -EAGAIN;
		}

		printf("  downloaded succeeded: %-20s [%7.2f K] (%d remaining)\n",
				repo->name, humanize_size(repo->buflen, 'K', NULL), remaining);
		decompress_repo_data(repo);
		repo->err = 0;
	}

	return 0;
}

static int hit_multi_handle_until_candy_comes_out(CURLM *multi)
{
	int active_handles;

	curl_multi_perform(multi, &active_handles);
	while(active_handles > 0) {
		int rc, maxfd =-1;
		long curl_timeout;;
		struct timeval timeout = { 1, 0 };
		fd_set fdread, fdwrite, fdexcep;

		curl_multi_timeout(multi, &curl_timeout);
		if(curl_timeout >= 0) {
			timeout.tv_sec = curl_timeout / 1000;
			if(timeout.tv_sec > 1) {
				timeout.tv_sec = 1;
			} else {
				timeout.tv_usec = (curl_timeout % 1000) * 1000;
			}
		}

		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdexcep);

		curl_multi_fdset(multi, &fdread, &fdwrite, &fdexcep, &maxfd);

		rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
		switch(rc) {
		case -1:
			fprintf(stderr, "error: failed to call select: %s\n", strerror(errno));
			break;
		case 0:
			/* timeout, fallthrough */
		default:
			curl_multi_perform(multi, &active_handles);
			break;
		}

		/* read any pending messages */
		for(;;) {
			int r = read_multi_msgs(multi, active_handles);
			if (r == -EAGAIN) {
				/* "ref" the active_handles -- there's still more to do */
				active_handles++;
			} else if (r == -ENOENT) {
				/* we're out of messages */
				break;
			}
		}
	}

	return 0;
}

int nosr_update(struct repo_t **repos, int repocount)
{
	int i, r, ret = 0;
	struct utsname un;
	CURLM *cmulti;

	if(access(CACHEPATH, W_OK)) {
		fprintf(stderr, "error: unable to write to %s: %s", CACHEPATH,
				strerror(errno));
		return 1;
	}

	curl_global_init(CURL_GLOBAL_ALL);
	cmulti = curl_multi_init();

	uname(&un);

	/* prime the handle by adding a URL from each repo */
	for(i = 0; i < repocount; i++) {
		repos[i]->arch = un.machine;
		r = add_repo_download(cmulti, repos[i]);
		if(r != 0) {
			ret = r;
		}
	}

	hit_multi_handle_until_candy_comes_out(cmulti);

	/* curl_multi_remove_handle(3) dictates we do this */
	for(i = 0; i < repocount; i++) {
		curl_multi_remove_handle(cmulti, repos[i]->curl);
		curl_easy_cleanup(repos[i]->curl);
		if(repos[i]->err != 0) {
			ret = 1;
		}
	}

	curl_multi_cleanup(cmulti);
	curl_global_cleanup();

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
