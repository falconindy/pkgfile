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

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <glob.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <curl/curl.h>

#include "macro.h"
#include "pkgfile.h"
#include "update.h"

struct archive_conv {
	struct archive *in;
	struct archive *out;
	struct archive_entry *ae;
	const char *reponame;
	char tmpfile[PATH_MAX];
	char diskfile[PATH_MAX];
};

static double simple_pow(int base, int exp)
{
	double result = 1.0;
	for(; exp > 0; exp--) {
		result *= base;
	}
	return result;
}

static double humanize_size(off_t bytes, const char target_unit, int precision,
		const char **label)
{
	static const char *labels[] = {"B", "KiB", "MiB", "GiB",
		"TiB", "PiB", "EiB", "ZiB", "YiB"};
	static const int unitcount = sizeof(labels) / sizeof(labels[0]);

	double val = (double)bytes;
	int index;

	for(index = 0; index < unitcount - 1; index++) {
		if(target_unit != '\0' && labels[index][0] == target_unit) {
			break;
		} else if(target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
			break;
		}
		val /= 1024.0;
	}

	if(label) {
		*label = labels[index];
	}

	/* fix FS#27924 so that it doesn't display negative zeroes */
	if(precision >= 0 && val < 0.0 &&
			val > (-0.5 / simple_pow(10, precision))) {
		val = 0.0;
	}

	return val;
}

static size_t strtrim(char *str)
{
	char *left = str, *right;

	if(!str || *str == '\0') {
		return 0;
	}

	while(isspace((unsigned char)*left)) {
		left++;
	}
	if(left != str) {
		memmove(str, left, (strlen(left) + 1));
	}

	if(*str == '\0') {
		return 0;
	}

	right = (char *)rawmemchr(str, '\0') - 1;
	while(isspace((unsigned char)*right)) {
		right--;
	}
	*++right = '\0';

	return right - left;
}

static char *strreplace(const char *str, const char *needle, const char *replace)
{
	const char *p, *q;
	char *newstr, *newp;
	char *list[8];
	int i, listsz = 0;
	size_t needlesz = strlen(needle), replacesz = strlen(replace);

	if(!str) {
		return NULL;
	}

	p = str;
	q = strstr(p, needle);
	while(q) {
		list[listsz++] = (char *)q;
		p = q + needlesz;
		q = strstr(p, needle);
	}

	/* no occurences of needle found */
	if(!listsz) {
		return strdup(str);
	}
	/* size of new string = size of old string + "number of occurences of needle"
	 * x "size difference between replace and needle" */
	CALLOC(newstr, strlen(str) + 1 + listsz * (replacesz - needlesz),
			sizeof(char), return NULL);

	p = str;
	newp = newstr;
	for(i = 0; i < listsz; i++) {
		q = list[i];
		if(q > p) {
			/* add chars between this occurence and last occurence, if any */
			memcpy(newp, p, (size_t)(q - p));
			newp += q - p;
		}
		memcpy(newp, replace, replacesz);
		newp += replacesz;
		p = q + needlesz;
	}

	if(*p) {
		/* add the rest of 'p' */
		strcpy(newp, p);
	}

	return newstr;
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

	if(asprintf(&temp, "%s/%s.files", string, repo) == -1) {
		fputs("error: failed to allocate memory\n", stderr);
	}

	free(string);

	return temp;
}

static char *split_keyval(char *line, const char *sep)
{
	strsep(&line, sep);
	return line;
}

static int parse_one_file(const char *, char **, struct repo_t ***, int *);

static int parse_include(const char *include, char **section,
		struct repo_t ***repos, int *repocount)
{
	glob_t globbuf;
	size_t i;

	if(glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
		fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
				include);
		return -ENOMEM;
	}

	for(i = 0; i < globbuf.gl_pathc; i++) {
		parse_one_file(globbuf.gl_pathv[i], section, repos, repocount);
	}

	globfree(&globbuf);

	return 0;
}

static int parse_one_file(const char *filename, char **section,
		struct repo_t ***repos, int *repocount)
{
	FILE *fp;
	char *ptr;
	char line[4096];
	const char * const server = "Server";
	const char * const include = "Include";
	int in_options = 0, r = 0, lineno = 0;
	struct repo_t **active_repos = *repos;

	fp = fopen(filename, "r");
	if(!fp) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return -errno;
	}

	while(fgets(line, sizeof(line), fp)) {
		size_t len;
		++lineno;

		/* remove comments */
		ptr = strchr(line, '#');
		if(ptr) {
			*ptr = '\0';
		}

		len = strtrim(line);
		if(len == 0) {
			continue;
		}

		/* found a section header */
		if(line[0] == '[' && line[len - 1] == ']') {
			free(*section);
			*section = strndup(&line[1], len - 2);
			if(len - 2 == 7 && memcmp(*section, "options", 7) == 0) {
				in_options = 1;
			} else {
				in_options = 0;
				active_repos = realloc(active_repos, sizeof(struct repo_t *) * (*repocount + 2));
				active_repos[*repocount] = repo_new(*section);
				(*repocount)++;
			}
		}

		if(strchr(line, '=')) {
			char *key = line, *val = split_keyval(line, "=");
			strtrim(key);
			strtrim(val);

			if(strcmp(key, server) == 0) {
				if(*section == NULL) {
					fprintf(stderr, "error: failed to parse %s on line %d: found 'Server' directive "
							"outside of a section\n", filename, lineno);
					continue;
				}
				if(in_options) {
					fprintf(stderr, "error: failed to parse %s on line %d: found 'Server' directive "
							"in options section\n", filename, lineno);
					continue;
				}
				r = repo_add_server(active_repos[*repocount - 1], val);
				if(r < 0) {
					break;
				}
			} else if(strcmp(key, include) == 0) {
				parse_include(val, section, &active_repos, repocount);
			}
		}
	}

	fclose(fp);

	*repos = active_repos;

	return r;
}

struct repo_t **find_active_repos(const char *filename, int *repocount)
{
	struct repo_t **repos = NULL;
	char *section = NULL;

	*repocount = 0;

	if(parse_one_file(filename, &section, &repos, repocount) < 0) {
		/* TODO: free repos on fail? */
		return NULL;
	}

	free(section);

	return repos;
}

static int endswith(const char *s, const char *postfix)
{
	size_t sl, pl;

	sl = strlen(s);
	pl = strlen(postfix);

	if(pl == 0 || sl < pl)
		return 0;

	return memcmp(s + sl - pl, postfix, pl) == 0;
}

static int write_cpio_entry(struct archive_conv *conv, const char *entryname)
{
	off_t entry_size = archive_entry_size(conv->ae);
	struct archive_read_buffer buf;
	char *slash, *entry_data, *s;
	int r, rc = -1, bytes_w = 0;
	size_t alloc_size;

	/* be generous */
	alloc_size = entry_size * 1.5;
	MALLOC(entry_data, alloc_size, return -1);

	memset(&buf, 0, sizeof(struct archive_read_buffer));
	MALLOC(buf.line, MAX_LINE_SIZE, return -1);

	/* discard the first line */
	archive_fgets(conv->in, &buf);

	while(archive_fgets(conv->in, &buf) == ARCHIVE_OK && buf.real_line_size > 0) {
		/* ensure enough memory */
		if(bytes_w + buf.real_line_size + 1 > alloc_size) {
			entry_data = realloc(entry_data, alloc_size * 1.5);
		}

		/* do the copy, with a slash prepended */
		entry_data[bytes_w++] = '/';
		memcpy(&entry_data[bytes_w], buf.line, buf.real_line_size);
		bytes_w += buf.real_line_size;
		entry_data[bytes_w++] = '\n';
	}

	/* adjust the entry size for removing the first line and adding slashes */
	archive_entry_set_size(conv->ae, bytes_w);

	/* store the metadata as simply $pkgname-$pkgver-$pkgrel */
	s = strdup(entryname);
	slash = strrchr(s, '/');
	*slash = '\0';
	archive_entry_update_pathname_utf8(conv->ae, s);
	free(s);

	if(archive_write_header(conv->out, conv->ae) != ARCHIVE_OK) {
		fprintf(stderr, "error: failed to write entry header: %s/%s: %s\n",
				conv->reponame, archive_entry_pathname(conv->ae), strerror(errno));
		goto error;
	}

	r = archive_write_data(conv->out, entry_data, bytes_w);
	if(r != bytes_w) {
		fprintf(stderr, "error: failed to write entry: %s/%s: %s\n",
				conv->reponame, archive_entry_pathname(conv->ae), strerror(errno));
		goto error;
	}

	rc = 0;

error:
	free(entry_data);
	free(buf.line);

	return rc;
}

static void archive_conv_close(struct archive_conv *conv)
{
	archive_write_close(conv->out);
	archive_write_free(conv->out);
	archive_read_close(conv->in);
	archive_read_free(conv->in);
}

static int archive_conv_open(struct archive_conv *conv,
		const struct repo_t *repo)
{
	int r;

	/* generally, repo files are gzip compressed, but there's no guarantee of
	 * this. in order to be compression-agnostic, use libarchive's reader/writer
	 * methods. this also gives us an opportunity to rewrite the archive as CPIO,
	 * which is marginally faster given our staunch sequential access. */

	conv->reponame = repo->name;
	r = snprintf(conv->tmpfile, PATH_MAX, CACHEPATH "/%s.files~", repo->name);
	memcpy(conv->diskfile, conv->tmpfile, r);
	conv->diskfile[r - 1] = '\0';

	conv->in = archive_read_new();
	conv->out = archive_write_new();

	if(conv->in == NULL || conv->out == NULL) {
		fputs("error: failed to allocate memory for archive objects\n", stderr);
		return -ENOMEM;
	}

	archive_read_support_format_tar(conv->in);
	archive_read_support_filter_all(conv->in);
	r = archive_read_open_memory(conv->in, repo->data, repo->buflen);
	if(r != ARCHIVE_OK) {
		fprintf(stderr, "error: failed to create archive reader for %s: %s\n",
				repo->name, archive_error_string(conv->in));
		goto open_error;
	}

	if(repo->config->compress > 0) {
		archive_write_add_filter(conv->out, repo->config->compress);
	}

	archive_write_set_format_cpio(conv->out);
	r = archive_write_open_filename(conv->out, conv->tmpfile);
	if(r != ARCHIVE_OK) {
		fprintf(stderr, "error: failed to open file for writing: %s: %s\n",
				conv->tmpfile, archive_error_string(conv->out));
		goto open_error;
	}

	return 0;

open_error:
	archive_write_free(conv->out);
	archive_read_free(conv->in);

	return -errno;
}

static int repack_repo_data(const struct repo_t *repo)
{
	struct archive_conv conv;
	int ret;

	if(archive_conv_open(&conv, repo) < 0) {
		return -1;
	}

	while(archive_read_next_header(conv.in, &conv.ae) == ARCHIVE_OK) {
		const char *entryname = archive_entry_pathname(conv.ae);

		/* ignore everything but the /files metadata */
		if(endswith(entryname, "/files")) {
			ret = write_cpio_entry(&conv, entryname);
			if(ret < 0) {
				break;
			}
		}
	}

	archive_conv_close(&conv);

	if(ret < 0) {
		/* oh noes! */
		if(unlink(conv.tmpfile) < 0 && errno != ENOENT) {
			fprintf(stderr, "error: failed to unlink temporary file: %s: %s\n", conv.tmpfile,
					strerror(errno));
		}
		return -1;
	}

	if(rename(conv.tmpfile, conv.diskfile) != 0) {
		fprintf(stderr, "error: failed to rotate new repo for %s into place: %s\n",
				repo->name, strerror(errno));
		return -1;
	}

	return 0;
}

static int repack_repo_data_async(struct repo_t *repo)
{
	repo->worker = fork();

	if(repo->worker < 0) {
		perror("warning: failed to fork new process");

		/* don't just give up, try to repack the repo synchronously */
		return repack_repo_data(repo);
	}

	if(repo->worker == 0) {
		exit(repack_repo_data(repo));
	}

	return 0;
}

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *data)
{
	void *writebuf;
	const size_t realsize = size * nmemb;
	size_t grow = 0;
	struct repo_t *repo = data;

	if(repo->buflen + realsize > repo->capacity) {
		double contentlen = 0;

		/* alloc at once */
		curl_easy_getinfo(repo->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentlen);
		if(contentlen >= 0) {
			grow = contentlen;
		} else {
			/* crappy mirror, fallback on an incremental growth */
			grow = repo->buflen + realsize;
		}
	}

	/* hey, i just alloc'd you and this is crazy, but I need to
	 * write more, so realloc maybe? */
	if(grow > 0) {
		writebuf = realloc(repo->data, grow + 1);
		if(!writebuf) {
			fprintf(stderr, "error: failed to reallocate %zd bytes\n", grow + 1);
			return 0;
		}
		repo->data = writebuf;
	}

	memcpy(&(repo->data[repo->buflen]), ptr, realsize);
	repo->buflen += realsize;
	repo->capacity = grow + 1;
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
		FREE(repo->data);
		repo->buflen = 0;
		repo->capacity = 0;
		repo->server_idx++;
	}

	if(repo->server_idx >= repo->servercount) {
		fprintf(stderr, "error: failed to update repo: %s\n", repo->name);
		return -1;
	}

	repo->url = prepare_url(repo->servers[repo->server_idx], repo->name, repo->arch);
	if(repo->url == NULL) {
		fputs("error: failed to allocate URL for download\n", stderr);
		return -1;
	}

	curl_easy_setopt(repo->curl, CURLOPT_URL, repo->url);

	if(repo->force == 0 && stat(repo->diskfile, &st) == 0) {
		curl_easy_setopt(repo->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
		curl_easy_setopt(repo->curl, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
	}

	gettimeofday(&repo->dl_time_start, NULL);
	curl_multi_add_handle(multi, repo->curl);

	return 0;
}

static double timediff(const struct timeval *start, const struct timeval *end)
{
	const double s_sec = start->tv_sec + (start->tv_usec / 1000000.0);
	const double e_sec = end->tv_sec + (end->tv_usec / 1000000.0);
	return e_sec - s_sec;
}

static double timediff_since(const struct timeval *since)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return timediff(since, &now);
}

static int print_rate(double xfer, const char *xfer_label,
		double rate, const char rate_label)
{
	/* We will show 1.62M/s, 11.6M/s, but 116K/s and 1116K/s */
	if(rate < 9.995) {
		return printf("%8.1f %3s  %4.2f%c/s", xfer, xfer_label, rate, rate_label);
	} else if(rate < 99.95) {
		return printf("%8.1f %3s  %4.1f%c/s", xfer, xfer_label, rate, rate_label);
	} else {
		return printf("%8.1f %3s  %4.f%c/s", xfer, xfer_label, rate, rate_label);
	}
}

static void print_download_success(struct repo_t *repo, int remaining)
{
	const char *rate_label, *xfered_label;
	double rate, xfered_human;
	int width;

	rate = repo->buflen / timediff_since(&repo->dl_time_start);
	xfered_human = humanize_size(repo->buflen, '\0', -1, &xfered_label);

	printf("  download complete: %-20s [", repo->name);
	if(fabs(rate - INFINITY) < DBL_EPSILON) {
		width = printf(" [%6.1f %3s  %7s ",
				xfered_human, xfered_label, "----");
	} else {
		double rate_human = humanize_size(rate, '\0', -1, &rate_label);
		width = print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
	}
	printf(" %*d remaining]\n", 23 - width, remaining);
}

static void print_total_dl_stats(int count, double duration, off_t total_xfer)
{
	const char *rate_label, *xfered_label;
	double rate, xfered_human, rate_human;
	int width;

	rate = total_xfer / duration;
	xfered_human = humanize_size(total_xfer, '\0', -1, &xfered_label);
	rate_human = humanize_size(rate, '\0', -1, &rate_label);

	width = printf(":: download complete in %.2fs", duration);
	printf("%*s<", 42 - width, "");
	print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
	printf(" %2d files    >\n", count);
}

static int read_multi_msg(CURLM *multi, int remaining)
{
	struct repo_t *repo;
	int msgs_left;
	CURLMsg *msg;

	msg = curl_multi_info_read(multi, &msgs_left);
	if(msg == NULL) {
		/* signal to the caller that we're out of messages */
		return -ENOENT;
	}

	curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &repo);
	if(msg->msg == CURLMSG_DONE) {
		long timecond, resp;

		curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &timecond);
		curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);

		if(timecond == 1) {
			printf("  %s is up to date\n", repo->name);
			repo->err = 1;
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

		print_download_success(repo, remaining);
		repack_repo_data_async(repo);
		repo->err = 0;
	}

	return 0;
}

static void hit_multi_handle_until_candy_comes_out(CURLM *multi)
{
	int active_handles;

	curl_multi_perform(multi, &active_handles);
	do {
		int rc = curl_multi_wait(multi, NULL, 0, -1, NULL);
		if(rc != CURLM_OK) {
			fprintf(stderr, "error: curl_multi_wait failed (%d)\n", rc);
			break;
		}

		rc = curl_multi_perform(multi, &active_handles);
		if(rc != CURLM_OK) {
			fprintf(stderr, "error: curl_multi_perform failed (%d)\n", rc);
			break;
		}

		/* read any pending messages */
		for(;;) {
			int r = read_multi_msg(multi, active_handles);
			if(r == -EAGAIN) {
				continue;
			} else if(r == -ENOENT) {
				/* we're out of messages */
				break;
			}
		}
	} while(active_handles > 0);
}


static int reap_children(struct repo_t **repos, int repocount)
{
	int i, r = 0, running = 0;

	/* immediately reap zombies, but don't wait on still active children */
	for(i = 0; i < repocount; i++) {
		int stat_loc;
		if(repos[i]->worker > 0) {
			if(wait4(repos[i]->worker, &stat_loc, WNOHANG, NULL) == 0) {
				running++;
			} else {
				/* exited, grab the status */
				r += WEXITSTATUS(stat_loc);
			}
		}
	}

	if(running > 0) {
		int stat_loc;
		printf(":: waiting for %d process%s to finish repacking repos...\n",
				running, running == 1 ? "" : "es");
		for(;;) {
			pid_t pid = wait4(-1, &stat_loc, 0, NULL);
			if(pid < 0 && errno == ECHILD) {
				/* no more children */
				break;
			} else if(pid > 0) {
				r += WEXITSTATUS(stat_loc);
			}
		}
	}

	return r;
}

int pkgfile_update(struct repo_t **repos, int repocount, struct config_t *config)
{
	int i, r, force, xfer_count = 0, ret = 0;
	struct utsname un;
	CURLM *cmulti;
	struct timeval t_start;
	off_t total_xfer = 0;
	double duration;

	if(access(CACHEPATH, W_OK)) {
		fprintf(stderr, "error: unable to write to " CACHEPATH ": %s\n",
				strerror(errno));
		return 1;
	}

	printf(":: Updating %d repos...\n", repocount);

	curl_global_init(CURL_GLOBAL_ALL);
	cmulti = curl_multi_init();
	if(cmulti == NULL) {
		/* this can only fail due to out an OOM condition */
		fprintf(stderr, "error: failed to initialize curl: %s\n", strerror(ENOMEM));
		return 1;
	}

	uname(&un);
	force = (config->doupdate > 1);

	/* ensure all our DBs are 0644 */
	umask(0022);

	/* prime the handle by adding a URL from each repo */
	for(i = 0; i < repocount; i++) {
		repos[i]->arch = un.machine;
		repos[i]->force = force;
		repos[i]->config = config;
		r = add_repo_download(cmulti, repos[i]);
		if(r != 0) {
			ret = r;
		}
	}

	gettimeofday(&t_start, NULL);
	hit_multi_handle_until_candy_comes_out(cmulti);
	duration = timediff_since(&t_start);

	/* remove handles, aggregate results */
	for(i = 0; i < repocount; i++) {
		curl_multi_remove_handle(cmulti, repos[i]->curl);
		curl_easy_cleanup(repos[i]->curl);

		FREE(repos[i]->url);
		FREE(repos[i]->data);

		total_xfer += repos[i]->buflen;

		switch(repos[i]->err) {
		case 0:
			xfer_count++;
			break;
		case -1:
			ret = 1;
			break;
		}
	}

	/* print transfer stats if we downloaded more than 1 file */
	if(xfer_count > 1) {
		print_total_dl_stats(xfer_count, duration, total_xfer);
	}

	if(reap_children(repos, repocount) != 0) {
		ret = 1;
	}

	curl_multi_cleanup(cmulti);
	curl_global_cleanup();

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
