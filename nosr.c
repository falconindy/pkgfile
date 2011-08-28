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

static struct config_t config;

static int archive_fgets(struct archive *a, struct archive_read_buffer *b)
{
	char *i = NULL;
#if ARCHIVE_VERSION_NUMBER < 3000000
	off_t offset;
#else
	int64_t offset;
#endif
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
		memset(b, 0, sizeof(b));
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

static int is_binary(const char *line)
{
	if(config.binaries &&
			(line[strlen(line)-1] == '/' ||
			!(strncmp(line, "bin/", 4) == 0 || strstr(line, "/bin/") ||
			strncmp(line, "sbin/", 5) == 0 || strstr(line, "/sbin/")))) {
		return 0;
	}

	return 1;
}

static int search_metafile(const char *repo, const char *pkgname,
		struct archive *a, struct result_t *result) {
	int found = 0;
	const char * const files = "%FILES%";
	struct archive_read_buffer buf;

	memset(&buf, 0, sizeof(buf));
	buf.max_line_size = 512 * 1024;

	while(archive_fgets(a, &buf) == ARCHIVE_OK) {
		size_t len = strip_newline(buf.line);
		char *line;

		if(!len || buf.line[len-1] == '/' || strcmp(buf.line, files) == 0 ||
				(config.binaries && !is_binary(buf.line))) {
			continue;
		}

		if(!found && config.filterfunc(&config.filter, buf.line, config.icase) == 0) {
			if(asprintf(&line, "%s/%s", repo, pkgname) == -1) {
				fprintf(stderr, "error: failed to allocate memory\n");
				return 1;
			};
			result_add(result, line);
			found = 1;
		}
	}

	return 1;
}

static int list_metafile(const char *repo, const char *pkgname,
		struct archive *a, struct result_t *result) {
	int ret;
	const char * const files = "%FILES%";
	struct archive_read_buffer buf;

	if(strcmp(pkgname, config.filter.glob) != 0) {
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

		if(config.binaries && !is_binary(buf.line)) {
			continue;
		}

		if(asprintf(&line, "%s/%s /%s", repo, pkgname, buf.line) == -1) {
			fprintf(stderr, "error: failed to allocate memory\n");
			return 1;
		}
		result_add(result, line);
	}

	return 1;
}

static char *parse_pkgname(const char *entryname)
{
	char *ptr, *pkgname;

	pkgname = strdup(entryname);
	if(!pkgname) {
		return NULL;
	}

	/* trim off pkgrel */
	ptr = strrchr(pkgname, '-');
	if(!ptr) {
		free(pkgname);
		return NULL;
	}
	*ptr = '\0';

	/* trim off pkgver */
	ptr = strrchr(pkgname, '-');
	if(!ptr) {
		free(pkgname);
		return NULL;
	}
	*ptr = '\0';

	return pkgname;
}

static void *load_repo(void *repo)
{
	int ret;
	const char *entryname, *slash;
	char *pkgname;
	char repofile[1024];
	struct archive *a;
	struct archive_entry *e;
	struct result_t *result;

	snprintf(repofile, 1024, "%s.files.tar.gz", (char *)repo);
	result = result_new((char *)repo, 50);

	a = archive_read_new();
	archive_read_support_compression_all(a);
	archive_read_support_format_all(a);

	ret = archive_read_open_filename(a, repofile, ARCHIVE_DEFAULT_BYTES_PER_BLOCK);
	if(ret != ARCHIVE_OK) {
		/* fail silently if the file doesn't exist */
		if(access(repofile, F_OK) == 0) {
			fprintf(stderr, "error: failed to load repo: %s: %s\n", repofile,
					archive_error_string(a));
		}
		goto cleanup;
	}

	while(archive_read_next_header(a, &e) == ARCHIVE_OK) {
		entryname = archive_entry_pathname(e);
		slash = strrchr(entryname, '/');
		if(!slash || strcmp(slash, "/files") != 0) {
			continue;
		}

		pkgname = parse_pkgname(entryname);
		if(!pkgname) {
			fprintf(stderr, "error parsing pkgname from: %s\n", entryname);
			continue;
		}

		ret = config.filefunc(repo, pkgname, a, result);
		free(pkgname);

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
	archive_read_finish(a);

	return (void *)result;
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
			" Options:\n"
			"  -b, --binaries          return only files contained in a bin dir\n"
			"  -g, --glob              enable matching with glob characters\n"
			"  -h, --help              display this help and exit\n"
			"  -i, --ignorecase        use case insensitive matching\n"
			"  -R, --repo REPO         search a specific repo\n"
			"  -r, --regex             enable matching with pcre\n"
			"  -l, --list              list contents of a package\n\n");
}

static int parse_opts(int argc, char **argv)
{
	int opt, opt_idx;
	static struct option opts[] = {
		{"binaries",    no_argument,        0, 'b'},
		{"glob",        no_argument,        0, 'g'},
		{"help",        no_argument,        0, 'h'},
		{"ignorecase",  no_argument,        0, 'i'},
		{"list",        no_argument,        0, 'l'},
		{"repo",        required_argument,  0, 'R'},
		{"regex",       no_argument,        0, 'r'},
		{"update",      no_argument,        0, 'u'},
		{0,0,0,0}
	};

	while((opt = getopt_long(argc, argv, "bghilR:ru", opts, &opt_idx)) != -1) {
		switch(opt) {
			case 'b':
				config.binaries = 1;
				break;
			case 'g':
				config.filterby = FILTER_GLOB;
				break;
			case 'h':
				usage();
				return 1;
			case 'i':
				config.icase = 1;
				break;
			case 'l':
				config.filefunc = list_metafile;
				break;
			case 'R':
				config.targetrepo = optarg;
				break;
			case 'r':
				config.filterby = FILTER_REGEX;
				break;
			case 'u':
				config.doupdate = 1;
				break;
			default:
				return 1;
		}
	}

	return 0;
}

static int search_single_repo(struct repo_t **repos, char *searchstring)
{
	char *targetrepo = NULL, *slash;

	if(config.targetrepo) {
		targetrepo = config.targetrepo;
		config.filter.glob = searchstring;
	} else {
		slash = strchr(searchstring, '/');
		targetrepo = strdup(searchstring);
		targetrepo[slash - searchstring] = '\0';
		config.filter.glob = &slash[1];
	}

	config.filterby = FILTER_EXACT;

	do {
		if(strcmp((*repos)->name, targetrepo) == 0) {
			struct result_t *result = load_repo(targetrepo);
			result_print(result);
			result_free(result);
			return 0;
		}
	} while(*(++repos));

	/* repo not found */
	fprintf(stderr, "error: repo not available: %s\n", targetrepo);

	if(!config.targetrepo) {
		free(targetrepo);
	}

	return 1;
}

int main(int argc, char *argv[])
{
	int i, repocount, ret = 0;
	pthread_t *t = NULL;
	struct repo_t **repos = NULL;
	struct result_t **results = NULL;

	config.filefunc = search_metafile;
	if(parse_opts(argc, argv) != 0) {
		return 2;
	}

	if(chdir(DBPATH)) {
		fprintf(stderr, "chdir: " DBPATH ": %s\n", strerror(errno));
		return 2;
	}

	repos = find_active_repos(PACMANCONFIG);
	for(repocount = 0; repos[repocount]; repocount++);
	if(!repocount) {
		fprintf(stderr, "error: no repos found in " PACMANCONFIG "\n");
		return 1;
	}

	if(config.doupdate) {
		nosr_update(repos);
		goto cleanup;
	}

	if(optind == argc) {
		fprintf(stderr, "error: no target specified (use -h for help)\n");
		goto cleanup;
	}

	switch(config.filterby) {
		case FILTER_EXACT:
			config.filter.glob = argv[optind];
			config.filterfunc = match_exact;
			break;
		case FILTER_GLOB:
			config.icase *= FNM_CASEFOLD;
			config.filter.glob = argv[optind];
			config.filterfunc = match_glob;
			break;
		case FILTER_REGEX:
			config.icase *= PCRE_CASELESS;
			config.filterfunc = match_regex;
			config.filterfree = free_regex;
			if(compile_pcre_expr(&config.filter.re, argv[optind], config.icase) != 0) {
				goto cleanup;
			}
			break;
	}

	/* override behavior on $repo/$pkg syntax or --repo */
	if((config.filefunc == list_metafile && strchr(argv[optind], '/')) ||
			config.targetrepo) {
		ret = search_single_repo(repos, argv[optind]);
		goto cleanup;
	}

	CALLOC(t, repocount, sizeof(pthread_t *), goto cleanup);
	CALLOC(results, repocount, sizeof(struct result_t *), goto cleanup);

	/* load and process DBs */
	for(i = 0; i < repocount; i++) {
		const char *filename = repos[i]->name;
		pthread_create(&t[i], NULL, load_repo, (void *)filename);
	}

	/* gather results */
	for(i = 0; i < repocount; i++) {
		pthread_join(t[i], (void **)&results[i]);
	}

	if(config.filterfree) {
		config.filterfree(&config.filter);
	}

	qsort(results, repocount, sizeof(struct result_t *), result_cmp);
	for(i = 0; i < repocount; i++) {
		ret += result_print(results[i]);
		result_free(results[i]);
	}
	ret = ret > 0 ? 0 : 1;

cleanup:
	for(i = 0; i < repocount; i++) {
		repo_free(repos[i]);
	}
	free(t);
	free(repos);
	free(results);

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
