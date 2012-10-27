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

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "pkgfile.h"
#include "macro.h"
#include "match.h"
#include "result.h"
#include "update.h"

#ifdef GIT_VERSION
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif

static struct config_t config;

static const char *filtermethods[] = {
	[FILTER_GLOB]  = "glob",
	[FILTER_REGEX] = "regex"
};

int archive_fgets(struct archive *a, struct archive_read_buffer *b)
{
	/* ensure we start populating our line buffer at the beginning */
	b->line_offset = b->line;

	while(1) {
		size_t new, block_remaining;
		char *eol;

		/* have we processed this entire block? */
		if(b->block + b->block_size == b->block_offset) {
			int64_t offset;
			if(b->ret == ARCHIVE_EOF) {
				/* reached end of archive on the last read, now we are out of data */
				return b->ret;
			}

			/* zero-copy - this is the entire next block of data. */
			b->ret = archive_read_data_block(a, (void *)&b->block,
					&b->block_size, &offset);
			b->block_offset = b->block;
			block_remaining = b->block_size;

			/* error, cleanup */
			if(b->ret < ARCHIVE_OK) {
				return b->ret;
			}
		} else {
			block_remaining = b->block + b->block_size - b->block_offset;
		}

		/* look through the block looking for EOL characters */
		eol = memchr(b->block_offset, '\n', block_remaining);
		if(!eol) {
			eol = memchr(b->block_offset, '\0', block_remaining);
		}

		/* note: we know eol > b->block_offset and b->line_offset > b->line,
		 * so we know the result is unsigned and can fit in size_t */
		new = eol ? (size_t)(eol - b->block_offset) : block_remaining;
		if((b->line_offset - b->line + new + 1) > MAX_LINE_SIZE) {
			return -ERANGE;
		}

		if(eol) {
			size_t len = (size_t)(eol - b->block_offset);
			memcpy(b->line_offset, b->block_offset, len);
			b->line_offset[len] = '\0';
			b->block_offset = eol + 1;
			b->real_line_size = b->line_offset + len - b->line;
			/* this is the main return point; from here you can read b->line */
			return ARCHIVE_OK;
		} else {
			/* we've looked through the whole block but no newline, copy it */
			size_t len = (size_t)(b->block + b->block_size - b->block_offset);
			b->line_offset = mempcpy(b->line_offset, b->block_offset, len);
			b->block_offset = b->block + b->block_size;
			/* there was no new data, return what is left; saved ARCHIVE_EOF will be
			 * returned on next call */
			if(len == 0) {
				b->line_offset[0] = '\0';
				b->real_line_size = b->line_offset - b->line;
				return ARCHIVE_OK;
			}
		}
	}

	return b->ret;
}

static bool is_binary(const char *line, const size_t len)
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

	if(
		 /* match bin/... */
		 (ptr == line) ||

		 /* match sbin/... */
		 (line == ptr - 1 && *(ptr - 1) == 's') ||

		 /* match .../bin/ */
		 (*(ptr - 1) == '/') ||

		 /* match .../sbin/ */
		 (ptr >= line + 2 && *(ptr - 2) == '/' && *(ptr - 1) == 's')) {

		/* ensure that we only match /bin/bar and not /bin/foo/bar */
		return memchr(ptr + 4, '/', (line + len) - (ptr + 4)) == NULL;
	}

	return false;
}

static bool is_directory(const char *line, const size_t len)
{
	return line[len - 1] == '/';
}

static int search_metafile(const char *repo, struct pkg_t *pkg,
		struct archive *a, struct result_t *result, struct archive_read_buffer *buf)
{
	int found = 0;

	while(archive_fgets(a, buf) == ARCHIVE_OK) {
		const size_t len = buf->real_line_size;

		if(len == 0) {
			continue;
		}

		if(!config.directories && is_directory(buf->line, len)) {
			continue;
		}

		if(config.binaries && !is_binary(buf->line, len)) {
			continue;
		}

		if(!found && config.filterfunc(&config.filter, buf->line, (int)len, config.icase) == 0) {
			char *line;
			if(config.verbose) {
				int prefixlen = asprintf(&line, "%s/%s %s", repo, pkg->name, pkg->version);
				if(prefixlen < 0) {
					fprintf(stderr, "error: failed to allocate memory\n");
					return -1;
				}
				result_add(result, line, buf->line, prefixlen);
				free(line);
			} else {
				found = 1;
				if(asprintf(&line, "%s/%s", repo, pkg->name) < 0) {
					fprintf(stderr, "error: failed to allocate memory\n");
					return -1;
				}
				result_add(result, line, NULL, 0);
				free(line);
			}
		}
	}

	return 0;
}

static int list_metafile(const char *repo, struct pkg_t *pkg,
		struct archive *a, struct result_t *result, struct archive_read_buffer *buf)
{
	if((config.icase ? strcasecmp : strcmp)(config.filter.glob.glob, pkg->name) != 0) {
		return 0;
	}

	while(archive_fgets(a, buf) == ARCHIVE_OK) {
		const size_t len = buf->real_line_size;
		int prefixlen = 0;
		char *line;

		if(len == 0 || (config.binaries && !is_binary(buf->line, len))) {
			continue;
		}

		if(config.quiet) {
			line = strdup(buf->line);
			if(line == NULL) {
				fprintf(stderr, "error: failed to allocate memory\n");
				return 0;
			}
		} else {
			prefixlen = asprintf(&line, "%s/%s", repo, pkg->name);
			if(prefixlen < 0) {
				fprintf(stderr, "error: failed to allocate memory\n");
				return 0;
			}
		}
		result_add(result, line, config.quiet ? NULL : buf->line, prefixlen);
		free(line);
	}

	return -1;
}

static int parse_pkgname(struct pkg_t *pkg, const char *entryname, size_t len)
{
	const char *dash, *slash = &entryname[len];

	if(!slash) {
		return -EINVAL;
	}

	dash = slash;
	while(dash > entryname && --dash && *dash != '-');
	while(dash > entryname && --dash && *dash != '-');

	if(*dash != '-') {
		return -EINVAL;
	}

	memcpy(pkg->name, entryname, len);

	/* ->name and ->version share the same memory */
	pkg->name[dash - entryname] = pkg->name[slash - entryname] = '\0';
	pkg->version = &pkg->name[dash - entryname + 1];

	return 0;
}

static void *load_repo(void *repo_obj)
{
	int fd = -1;
	char repofile[FILENAME_MAX];
	char *line;
	struct archive *a;
	struct archive_entry *e;
	struct pkg_t pkg;
	struct repo_t *repo;
	struct result_t *result;
	struct stat st;
	void *repodata = MAP_FAILED;
	struct archive_read_buffer read_buffer;

	repo = repo_obj;
	snprintf(repofile, sizeof(repofile), CACHEPATH "/%s.files", repo->name);
	result = result_new(repo->name, 50);

	a = archive_read_new();
	archive_read_support_format_all(a);
	archive_read_support_compression_all(a);

	memset(&read_buffer, 0, sizeof(struct archive_read_buffer));
	MALLOC(line, MAX_LINE_SIZE, return NULL);

	fd = open(repofile, O_RDONLY);
	if(fd < 0) {
		/* fail silently if the file doesn't exist */
		if(errno != ENOENT) {
			fprintf(stderr, "error: failed to open repo: %s: %s\n", repofile,
					strerror(errno));
		}
		goto cleanup;
	}

	repo->filefound = 1;

	fstat(fd, &st);
	repodata = mmap(0, st.st_size, PROT_READ, MAP_SHARED|MAP_POPULATE, fd, 0);
	if(repodata == MAP_FAILED) {
		fprintf(stderr, "error: failed to map pages for %s: %s\n", repofile,
				strerror(errno));
		goto cleanup;
	}

	if(archive_read_open_memory(a, repodata, st.st_size) != ARCHIVE_OK) {
		fprintf(stderr, "error: failed to load repo: %s: %s\n", repofile,
				archive_error_string(a));
		goto cleanup;
	}

	while(archive_read_next_header(a, &e) == ARCHIVE_OK) {
		const char *entryname = archive_entry_pathname(e);
		size_t len;
		int r;

		if(entryname == NULL) {
			/* libarchive error */
			continue;
		}

		len = strlen(entryname);
		r = parse_pkgname(&pkg, entryname, len);
		if(r < 0) {
			fprintf(stderr, "error parsing pkgname from: %s: %s\n", entryname,
					strerror(-r));
			continue;
		}

		memset(&read_buffer, 0, sizeof(struct archive_read_buffer));
		read_buffer.line = line;
		r = config.filefunc(repo->name, &pkg, a, result, &read_buffer);
		if(r < 0) {
			break;
		}
	}

	archive_read_close(a);

cleanup:
	free(line);
	archive_read_finish(a);
	if(fd >= 0) {
		close(fd);
	}
	if(repodata != MAP_FAILED) {
		munmap(repodata, st.st_size);
	}

	return result;
}

static int compile_pcre_expr(struct pcre_data *re, const char *preg, int flags)
{
	const char *err;
	int err_offset;

	re->re = pcre_compile(preg, flags, &err, &err_offset, NULL);
	if(!re->re) {
		fprintf(stderr, "error: failed to compile regex at char %d: %s\n", err_offset, err);
		return 1;
	}

	re->re_extra = pcre_study(re->re, PCRE_STUDY_JIT_COMPILE, &err);
	if(err) {
		fprintf(stderr, "error: failed to study regex: %s\n", err);
		pcre_free(re->re);
		return 1;
	}

	return 0;
}

static compresstype_t validate_compression(const char *compress)
{
	if(strcmp(compress, "none") == 0) {
		return COMPRESS_NONE;
	} else if(strcmp(compress, "gzip") == 0) {
		return COMPRESS_GZIP;
	} else if(strcmp(compress, "bzip2") == 0) {
		return COMPRESS_BZIP2;
	} else if(strcmp(compress, "lzma") == 0) {
		return COMPRESS_LZMA;
	} else if(strcmp(compress, "xz") == 0) {
		return COMPRESS_XZ;
	} else {
		return COMPRESS_INVALID;
	}
}

static void usage(void)
{
	fputs("pkgfile " PACKAGE_VERSION "\nUsage: pkgfile [options] target\n\n", stdout);
	fputs(
			" Operations:\n"
			"  -l, --list              list contents of a package\n"
			"  -s, --search            search for packages containing the target (default)\n"
			"  -u, --update            update repo files lists\n\n",
			stdout);
	fputs(
			" Matching:\n"
			"  -b, --binaries          return only files contained in a bin dir\n"
			"  -d, --directories       match directories in searches\n"
			"  -g, --glob              enable matching with glob characters\n"
			"  -i, --ignorecase        use case insensitive matching\n"
			"  -R, --repo REPO         search a singular repo\n"
			"  -r, --regex             enable matching with regular expressions\n\n",
			stdout);
	fputs(
			" Output:\n"
			"  -q, --quiet             output less when listing\n"
			"  -v, --verbose           output more\n"
			"  -w, --raw               disable output justification\n"
			"  -0, --null              null terminate output\n\n",
			stdout);
	fputs(
			" Downloading:\n"
			"  -z, --compress[=type]   compress downloaded repos\n\n",
			stdout);
	fputs(
			" General:\n"
			"  -h, --help              display this help and exit\n"
			"  -V, --version           display the version and exit\n\n",
			stdout);
}

static void print_version(void)
{
	fputs(PACKAGE_NAME " v" PACKAGE_VERSION "\n", stdout);
}

static int parse_opts(int argc, char **argv)
{
	int opt;
	static const char *shortopts = "0bdghilqR:rsuVvwz::";
	static const struct option longopts[] = {
		{"binaries",    no_argument,        0, 'b'},
		{"compress",    optional_argument,  0, 'z'},
		{"directories", no_argument,        0, 'd'},
		{"glob",        no_argument,        0, 'g'},
		{"help",        no_argument,        0, 'h'},
		{"ignorecase",  no_argument,        0, 'i'},
		{"list",        no_argument,        0, 'l'},
		{"quiet",       no_argument,        0, 'q'},
		{"repo",        required_argument,  0, 'R'},
		{"regex",       no_argument,        0, 'r'},
		{"search",      no_argument,        0, 's'},
		{"update",      no_argument,        0, 'u'},
		{"version",     no_argument,        0, 'V'},
		{"verbose",     no_argument,        0, 'v'},
		{"raw",         no_argument,        0, 'w'},
		{"null",        no_argument,        0, '0'},
		{0,0,0,0}
	};

	/* defaults */
	config.filefunc = search_metafile;
	config.eol = '\n';

	for(;;) {
		opt = getopt_long(argc, argv, shortopts, longopts, NULL);
		if(opt < 0) {
			break;
		}
		switch(opt) {
		case '0':
			config.eol = '\0';
			break;
		case 'b':
			config.binaries = true;
			break;
		case 'd':
			config.directories = true;
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
			exit(EXIT_SUCCESS);
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
			config.doupdate++;
			break;
		case 'V':
			print_version();
			exit(EXIT_SUCCESS);
		case 'v':
			config.verbose = true;
			break;
		case 'w':
			config.raw = true;
			break;
		case 'z':
			if(optarg != NULL) {
				config.compress = validate_compression(optarg);
				if(config.compress == COMPRESS_INVALID) {
					fprintf(stderr, "error: invalid compression option %s\n", optarg);
					return 1;
				}
			} else {
				config.compress = COMPRESS_GZIP;
			}
			break;
		default:
			return 1;
		}
	}

	return 0;
}

static int search_single_repo(struct repo_t **repos, int repocount, char *searchstring)
{
	char *targetrepo;
	int i;

	targetrepo = config.targetrepo ? config.targetrepo : strsep(&searchstring, "/");
	config.filter.glob.glob = searchstring;
	config.filterby = FILTER_EXACT;

	for(i = 0; i < repocount; i++) {
		if(strcmp(repos[i]->name, targetrepo) == 0) {
			struct result_t *result = load_repo(repos[i]);
			result_print(result, config.raw ? 0 : result->max_prefixlen, config.eol);
			result_free(result);
			return result->count == 0;
		}
	}

	/* repo not found */
	fprintf(stderr, "error: repo not available: %s\n", targetrepo);

	return 1;
}

static struct result_t **search_all_repos(struct repo_t **repos, int repocount)
{
	struct result_t **results;
	pthread_t *t = NULL;
	int i;

	CALLOC(t, repocount, sizeof(pthread_t), return NULL);
	CALLOC(results, repocount, sizeof(struct result_t *), return NULL);

	/* load and process DBs */
	for(i = 0; i < repocount; i++) {
		pthread_create(&t[i], NULL, load_repo, repos[i]);
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
	config.filter.glob.globlen = strlen(arg);

	switch(config.filterby) {
	case FILTER_EXACT:
		config.filter.glob.glob = arg;
		config.filterfunc = strchr(arg, '/') ? match_exact : match_exact_basename;
		break;
	case FILTER_GLOB:
		config.icase *= FNM_CASEFOLD;
		config.filter.glob.glob = arg;
		config.filterfunc = match_glob;
		break;
	case FILTER_REGEX:
		config.icase *= PCRE_CASELESS;
		config.filterfunc = match_regex;
		config.filterfree = free_regex;
		return compile_pcre_expr(&config.filter.re, arg, config.icase);
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

	repos = find_active_repos(PACMANCONFIG, &repocount);
	if(!repocount) {
		fprintf(stderr, "error: no repos found in " PACMANCONFIG "\n");
		return 1;
	}

	if(config.doupdate) {
		ret = !!pkgfile_update(repos, repocount, &config);
		goto cleanup;
	}

	if(optind == argc) {
		fprintf(stderr, "error: no target specified (use -h for help)\n");
		goto cleanup;
	}

	/* sanity check */
	if(config.filefunc == list_metafile && config.filterby != FILTER_EXACT) {
		fprintf(stderr, "error: --regex and --glob cannot be used with --list\n");
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
		int prefixlen;
		results = search_all_repos(repos, repocount);

		prefixlen = config.raw ? 0 : results_get_prefixlen(results, repocount);
		for(ret = i = 0; i < repocount; i++) {
			reposfound += repos[i]->filefound;
			ret += (int)result_print(results[i], prefixlen, config.eol);
			result_free(results[i]);
		}

		if(!reposfound) {
			fprintf(stderr, "error: No repo files found. Please run `pkgfile --update'.\n");
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
