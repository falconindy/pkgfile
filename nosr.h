#ifndef _NOSR_H
#define _NOSR_H

#include <archive.h>
#include <pcre.h>

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

struct task_t {
	const char *repofile;
	int (*filterfunc)(void*);
	void *filterarg;
};

typedef enum _filterstyle_t {
	FILTER_EXACT = 0,
	FILTER_GLOB,
	FILTER_REGEX
} filterstyle_t;

struct config_t {
	filterstyle_t filterby;
	int (*filefunc)(const char *repo, const char *entryname, struct archive* a);

	union {
		struct pcre_data re;
		char *glob;
	} filter;

	int icase;
	int icase_flag;
};

#endif /* _NOSR_H */
/* vim: set ts=2 sw=2 noet: */
