/*
 * Copyright (C) 2011 by Dave Reisner <dreisner@archlinux.org>
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
#include <fnmatch.h>
#include <getopt.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "nosr.h"
#include "match.h"
#include "result.h"
#include "update.h"
#include "util.h"

#ifndef BUFSIZ
# define BUFSIZ 8192
#endif

static struct config_t config;

static const char *filtermethods[] = {
	[FILTER_GLOB]  = "glob",
	[FILTER_REGEX] = "regex"
};

static int archive_fgets(struct archive *a, struct archive_read_buffer *b)
{
	char *i = NULL;
	off_t offset;
	int done = 0;

	/* ensure we start populating our line buffer at the beginning */
	b->line_offset = b->line;

	while(1) {
		/* have we processed this entire block? */
		if(b->block + b->block_size == b->block_offset) {
			if(b->ret == ARCHIVE_EOF) {
				/* reached end of archive on the last read, now we are out of data */
				goto cleanup;
			}

			/* zero-copy - this is the entire next block of data. */
			b->ret = archive_read_data_block(a, (void *)&b->block,
					&b->block_size, &offset);
			b->block_offset = b->block;

			/* error, cleanup */
			if(b->ret < ARCHIVE_OK) {
				goto cleanup;
			}
		}

		/* loop through the block looking for EOL characters */
		for(i = b->block_offset; i < (b->block + b->block_size); i++) {
			/* check if read value was null or newline */
			if(*i == '\0' || *i == '\n') {
				done = 1;
				break;
			}
		}

		/* allocate our buffer, or ensure our existing one is big enough */
		if(!b->line) {
			/* set the initial buffer to the read block_size */
			CALLOC(b->line, b->block_size + 1, sizeof(char), b->ret = -ENOMEM; goto cleanup);
			b->line_size = b->block_size + 1;
			b->line_offset = b->line;
		} else {
			size_t needed = (size_t)((b->line_offset - b->line)
					+ (i - b->block_offset) + 1);
			if(needed > b->max_line_size) {
				b->ret = -ERANGE;
				goto cleanup;
			}
			if(needed > b->line_size) {
				/* need to realloc + copy data to fit total length */
				char *new;
				CALLOC(new, needed, sizeof(char), b->ret = -ENOMEM; goto cleanup);
				memcpy(new, b->line, b->line_size);
				b->line_size = needed;
				b->line_offset = new + (b->line_offset - b->line);
				free(b->line);
				b->line = new;
			}
		}

		if(done) {
			size_t len = (size_t)(i - b->block_offset);
			memcpy(b->line_offset, b->block_offset, len);
			b->line_offset[len] = '\0';
			b->block_offset = ++i;
			/* this is the main return point; from here you can read b->line */
			return ARCHIVE_OK;
		} else {
			/* we've looked through the whole block but no newline, copy it */
			size_t len = (size_t)(b->block + b->block_size - b->block_offset);
			memcpy(b->line_offset, b->block_offset, len);
			b->line_offset += len;
			b->block_offset = i;
			/* there was no new data, return what is left; saved ARCHIVE_EOF will be
			 * returned on next call */
			if(len == 0) {
				b->line_offset[0] = '\0';
				return ARCHIVE_OK;
			}
		}
	}

cleanup:
	{
		int ret = b->ret;
		FREE(b->line);
		memset(b, 0, sizeof(struct archive_read_buffer));
		return ret;
	}
}

static size_t strip_newline(char *str)
{
	size_t len;
	if(str == '\0') {
		return 0;
	}
	len = strlen(str);
	while(len > 0 && str[len - 1] == '\n') {
		len--;
	}
	str[len] = '\0';

	return len;
}

static bool is_binary(const char *line, size_t len)
{
	const char *ptr;

	/* directories aren't binaries */
	if(line[len - 1] == '/') {
		return false;
	}

	ptr = memmem(line, len, "bin/", 4);

	/* toss out the obvious non-matches */
	if(!ptr) {
		return false;
	}

	/* match bin/... */
	if(ptr == line) {
		return true;
	}

	/* match sbin/... */
	if(line == ptr - 1 && *(ptr - 1) == 's') {
		return true;
	}

	/* match .../bin/ */
	if(*(ptr - 1) == '/') {
		return true;
	}

	/* match .../sbin/ */
	if(ptr >= line + 2 && *(ptr - 2) == '/' && *(ptr - 1) == 's') {
		return true;
	}

	return false;
}

static int search_metafile(const char *repo, struct pkg_t *pkg,
		struct archive *a, struct result_t *result) {
	int found = 0;
	const char * const files = "%FILES%";
	struct archive_read_buffer buf;

	memset(&buf, 0, sizeof(buf));
	buf.max_line_size = 512 * 1024;

	while(archive_fgets(a, &buf) == ARCHIVE_OK) {
		const size_t len = strip_newline(buf.line);
		char *line;

		if(!len || buf.line[len-1] == '/' || strcmp(buf.line, files) == 0 ||
				(config.binaries && !is_binary(buf.line, len))) {
			continue;
		}

		if(!found && config.filterfunc(&config.filter, buf.line, len, config.icase) == 0) {
			if(config.verbose) {
				if(asprintf(&line, "%s/%s %s\t/%s", repo, pkg->name, pkg->version, buf.line) == -1) {
					fprintf(stderr, "error: failed to allocate memory\n");
					return -1;
				};
			} else {
				found = 1;
				if(asprintf(&line, "%s/%s", repo, pkg->name) == -1) {
					fprintf(stderr, "error: failed to allocate memory\n");
					return -1;
				};
			}
			result_add(result, line);
		}
	}

	return 1;
}

static int list_metafile(const char *repo, struct pkg_t *pkg,
		struct archive *a, struct result_t *result) {
	int ret;
	const char * const files = "%FILES%";
	struct archive_read_buffer buf;

	if(config.filterfunc(&config.filter, pkg->name, (size_t)-1, config.icase) != 0) {
		return 1;
	}

	memset(&buf, 0, sizeof(buf));
	buf.max_line_size = 512 * 1024;

	while((ret = archive_fgets(a, &buf)) == ARCHIVE_OK) {
		size_t len = strip_newline(buf.line);
		char *line;

		if(!len || strcmp(buf.line, files) == 0) {
			continue;
		}

		if(config.binaries && !is_binary(buf.line, len)) {
			continue;
		}

		if(config.quiet) {
			if(asprintf(&line, "/%s", buf.line) == -1) {
				fprintf(stderr, "error: failed to allocate memory\n");
				return 1;
			}
		} else {
			if(asprintf(&line, "%s/%s /%s", repo, pkg->name, buf.line) == -1) {
				fprintf(stderr, "error: failed to allocate memory\n");
				return 1;
			}
		}
		result_add(result, line);
	}

	return 1;
}

static int parse_pkgname(struct pkg_t *pkg, const char *entryname)
{
	const char *slash, *ptr = strrchr(entryname, '-');

	if(ptr) {
		slash = ptr;
		while(--ptr && ptr > entryname && *ptr != '-');
		while(*++slash && *slash != '/');

		if(*slash == '/' && *ptr == '-') {
			pkg->name = strndup(entryname, ptr - entryname);
			pkg->version = strndup(ptr + 1, slash - ptr - 1);
			return 0;
		}
	}

	return 1;
}

static void *load_repo(void *repo_obj)
{
	int ret;
	const char *entryname, *slash;
	char repofile[1024];
	struct archive *a;
	struct archive_entry *e;
	struct pkg_t *pkg;
	struct repo_t *repo;
	struct result_t *result;

	repo = (struct repo_t *)repo_obj;
	snprintf(repofile, 1024, "%s.files.tar.gz", repo->name);
	result = result_new((char *)repo->name, 50);
	CALLOC(pkg, 1, sizeof(struct pkg_t), return (void *)result);

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_all(a);

	ret = archive_read_open_filename(a, repofile, BUFSIZ);
	if(ret != ARCHIVE_OK) {
		/* fail silently if the file doesn't exist */
		if(access(repofile, F_OK) == 0) {
			fprintf(stderr, "error: failed to load repo: %s: %s\n", repofile,
					archive_error_string(a));
		}
		goto cleanup;
	}

	repo->filefound = 1;
	while(archive_read_next_header(a, &e) == ARCHIVE_OK) {
		entryname = archive_entry_pathname(e);
		slash = strrchr(entryname, '/');
		if(!slash || strcmp(slash, "/files") != 0) {
			continue;
		}

		ret = parse_pkgname(pkg, entryname);
		if(ret != 0) {
			fprintf(stderr, "error parsing pkgname from: %s\n", entryname);
			continue;
		}

		ret = config.filefunc(repo->name, pkg, a, result);

		/* clean out the struct, but don't get rid of it entirely */
		free(pkg->name);
		free(pkg->version);

		switch(ret) {
			case -1:
				/* error */
				/* FALLTHROUGH */
			case 0:
				/* done */
				goto done;
			case 1:
				/* continue */
				break;
		}
	}
done:
	archive_read_close(a);

cleanup:
	free(pkg);
	archive_read_finish(a);

	return (void *)result;
}

static int compile_pcre_expr(struct pcre_data *re, const char *preg, int flags)
{
	const char *err;
	char *anchored = NULL;
	int err_offset;

	/* did the user try to anchor this at BOL? */
	if(preg[0] == '^') {
		/* goddamnit, they did. cut off the first character. Conditionally also
		 * drop the second character if its a slash. This is ugly and hackish, but
		 * it's also a fairly odd edge case. --glob is a much better choice here,
		 * as its self-anchoring. */
		preg++;
		if(preg[0] == '/') {
			preg++;
		}
		anchored = strdup(preg);
		preg = anchored;
		flags |= PCRE_ANCHORED;
	}

	re->re = pcre_compile(preg, flags, &err, &err_offset, NULL);
	free(anchored);

	if(!re->re) {
		fprintf(stderr, "error: failed to compile regex at char %d: %s\n", err_offset, err);
		return 1;
	}
	re->re_extra = pcre_study(re->re, 0, &err);
	if(err) {
		fprintf(stderr, "error: failed to study regex: %s\n", err);
		pcre_free(re->re);
		return 1;
	}

	return 0;
}

static void usage(void)
{
	fprintf(stderr, "nosr " VERSION "\nUsage: nosr [options] target\n\n");
	fprintf(stderr,
			" Operations:\n"
			"  -l, --list              list contents of a package\n"
			"  -s, --search            search for packages containing the target (default)\n"
			"  -u, --update            update repo files lists\n\n"
			" Filtering:\n"
			"  -b, --binaries          return only files contained in a bin dir\n"
			"  -g, --glob              enable matching with glob characters\n"
			"  -i, --ignorecase        use case insensitive matching\n"
			"  -q, --quiet             output less when listing\n"
			"  -R, --repo REPO         search a specific repo\n"
			"  -r, --regex             enable matching with pcre\n\n"
			"  -h, --help              display this help and exit\n"
			"  -v, --verbose           output more\n\n");
}

static int parse_opts(int argc, char **argv)
{
	int opt, opt_idx;
	const char *argv0_base;
	static const struct option opts[] = {
		{"binaries",    no_argument,        0, 'b'},
		{"glob",        no_argument,        0, 'g'},
		{"help",        no_argument,        0, 'h'},
		{"ignorecase",  no_argument,        0, 'i'},
		{"list",        no_argument,        0, 'l'},
		{"quiet",       no_argument,        0, 'q'},
		{"repo",        required_argument,  0, 'R'},
		{"regex",       no_argument,        0, 'r'},
		{"search",      no_argument,        0, 's'},
		{"update",      no_argument,        0, 'u'},
		{"verbose",     no_argument,        0, 'v'},
		{0,0,0,0}
	};

	/* defaults */
	config.filefunc = search_metafile;

	/* catch nosr-update for cron jobs */
	argv0_base = strrchr(argv[0], '/');
	if(argv0_base) {
		++argv0_base;
	} else {
		argv0_base = argv[0];
	}

	if(strcmp(argv0_base, "nosr-update") == 0) {
		config.doupdate = 1;
	}

	while((opt = getopt_long(argc, argv, "bghilqR:rsuv", opts, &opt_idx)) != -1) {
		switch(opt) {
			case 'b':
				config.binaries = true;
				break;
			case 'g':
				if(config.filterby != FILTER_EXACT) {
					fprintf(stderr, "error: --glob cannot be used with --%s option\n",
							filtermethods[config.filterby]);
					return 1;
				}
				config.filterby = FILTER_GLOB;
				break;
			case 'h':
				usage();
				return 1;
			case 'i':
				config.icase = true;
				break;
			case 'l':
				config.filefunc = list_metafile;
				break;
			case 'q':
				config.quiet = true;
				break;
			case 'R':
				config.targetrepo = optarg;
				break;
			case 'r':
				if(config.filterby != FILTER_EXACT) {
					fprintf(stderr, "error: --regex cannot be used with --%s option\n",
							filtermethods[config.filterby]);
					return 1;
				}
				config.filterby = FILTER_REGEX;
				break;
			case 's':
				config.filefunc = search_metafile;
				break;
			case 'u':
				config.doupdate = true;
				break;
			case 'v':
				config.verbose = true;
				break;
			default:
				return 1;
		}
	}

	return 0;
}

static int search_single_repo(struct repo_t **repos, int repocount, char *searchstring)
{
	char *targetrepo = NULL, *slash;
	int i, ret = 1;

	if(config.targetrepo) {
		targetrepo = config.targetrepo;
	} else {
		slash = strchr(searchstring, '/');
		targetrepo = strdup(searchstring);
		targetrepo[slash - searchstring] = '\0';
		config.filter.glob = &slash[1];
	}

	config.filterby = FILTER_EXACT;

	for(i = 0; i < repocount; i++) {
		if(strcmp(repos[i]->name, targetrepo) == 0) {
			struct result_t *result = load_repo((void *)repos[i]);
			ret = (result->count == 0);
			result_print(result);
			result_free(result);
			goto finish;
		}
	}

	/* repo not found */
	fprintf(stderr, "error: repo not available: %s\n", targetrepo);

finish:
	if(!config.targetrepo) {
		free(targetrepo);
	}

	return ret;
}

static struct result_t **search_all_repos(struct repo_t **repos, int repocount)
{
	struct result_t **results;
	pthread_t *t = NULL;
	int i;

	CALLOC(t, repocount, sizeof(pthread_t *), return NULL);
	CALLOC(results, repocount, sizeof(struct result_t *), return NULL);

	/* load and process DBs */
	for(i = 0; i < repocount; i++) {
		pthread_create(&t[i], NULL, load_repo, (void *)repos[i]);
	}

	/* gather results */
	for(i = 0; i < repocount; i++) {
		pthread_join(t[i], (void **)&results[i]);
	}

	free(t);

	return results;
}

static int filter_setup(char *arg)
{
	switch(config.filterby) {
		case FILTER_EXACT:
			config.filter.glob = arg;
			config.filterfunc = match_exact;
			break;
		case FILTER_GLOB:
			config.icase *= FNM_CASEFOLD;
			config.filter.glob = arg;
			config.filterfunc = match_glob;
			break;
		case FILTER_REGEX:
			config.icase *= PCRE_CASELESS;
			config.filterfunc = match_regex;
			config.filterfree = free_regex;
			if(compile_pcre_expr(&config.filter.re, arg, config.icase) != 0) {
				return 1;
			}
			break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int i, repocount, reposfound = 0, ret = 1;
	struct repo_t **repos = NULL;
	struct result_t **results = NULL;

	if(parse_opts(argc, argv) != 0) {
		return 2;
	}

	if(mkdir(CACHEPATH, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "error: failed to create cachedir: " CACHEPATH ": %s\n", strerror(errno));
		return 2;
	}

	if(chdir(CACHEPATH)) {
		fprintf(stderr, "error: failed to chdir to " CACHEPATH ": %s\n", strerror(errno));
		return 2;
	}

	repos = find_active_repos(PACMANCONFIG, &repocount);
	if(!repocount) {
		fprintf(stderr, "error: no repos found in " PACMANCONFIG "\n");
		return 1;
	}

	if(config.doupdate) {
		ret = !!nosr_update(repos, repocount);
		goto cleanup;
	}

	if(optind == argc) {
		fprintf(stderr, "error: no target specified (use -h for help)\n");
		goto cleanup;
	}

	/* sanity check */
	if(config.filefunc == list_metafile && config.filterby != FILTER_EXACT) {
		fprintf(stderr, "error: --regex and --glob are not allowed with --list\n");
		goto cleanup;
	}

	if(filter_setup(argv[optind]) != 0) {
		goto cleanup;
	}

	/* override behavior on $repo/$pkg syntax or --repo */
	if((config.filefunc == list_metafile && strchr(argv[optind], '/')) ||
			config.targetrepo) {
		ret = search_single_repo(repos, repocount, argv[optind]);
	} else {
		results = search_all_repos(repos, repocount);
		for(ret = i = 0; i < repocount; i++) {
			reposfound += repos[i]->filefound;
			ret += result_print(results[i]);
			result_free(results[i]);
		}

		if(!reposfound) {
			fprintf(stderr, "error: No repo files found. Please run `nosr --update'.\n");
		}

		ret = ret > 0 ? 0 : 1;
	}

	if(config.filterfree) {
		config.filterfree(&config.filter);
	}

cleanup:
	for(i = 0; i < repocount; i++) {
		repo_free(repos[i]);
	}
	free(repos);
	free(results);

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
