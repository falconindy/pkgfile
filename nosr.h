#ifndef _NOSR_H
#define _NOSR_H

#include <archive.h>
#include <archive_entry.h>

#include <pcre.h>

#include "result.h"

#define DBPATH  "/var/cache/nosr"

struct archive_read_buffer {
	char *line;
	char *line_offset;
	size_t line_size;
	size_t max_line_size;

	char *block;
	char *block_offset;
	size_t block_size;

	int ret;
};

struct pcre_data {
	pcre *re;
	pcre_extra *re_extra;
};

typedef enum _filterstyle_t {
	FILTER_EXACT = 0,
	FILTER_GLOB,
	FILTER_REGEX
} filterstyle_t;

typedef union _filterpattern_t {
	struct pcre_data re;
	char *glob;
} filterpattern_t;

struct pkg_t {
	char *name;
	char *version;
};

struct config_t {
	filterstyle_t filterby;
	filterpattern_t filter;
	int (*filefunc)(const char *repo, struct pkg_t *pkg, struct archive* a,
			struct result_t *result);
	int (*filterfunc)(filterpattern_t *filter, const char *line, int flags);
	void (*filterfree)(filterpattern_t *filter);
	char *targetrepo;
	int binaries;
	int icase;
	int doupdate;
	int verbose;
};

#endif /* _NOSR_H */
/* vim: set ts=2 sw=2 noet: */
