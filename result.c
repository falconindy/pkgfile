#define _GNU_SOURCE
#include <string.h>

#include "result.h"
#include "util.h"

static int result_grow(struct result_t *result)
{
	size_t newsz = result->maxcount * 2.5;
	result->list = realloc(result->list, newsz * sizeof(char *));
	if(!result->list) {
		return 1;
	}

	result->maxcount = newsz;

	return 0;
}

struct result_t *result_new(char *name, size_t initial_size)
{
	struct result_t *result;

	CALLOC(result, 1, sizeof(struct result_t), return NULL);

	result->list = calloc(initial_size, sizeof(char *));
	if(!result->list) {
		free(result);
		return NULL;
	}

	result->name = strdup(name);
	result->maxcount = initial_size;

	return result;
}

int result_add(struct result_t *result, char *name)
{
	if(!result || !name) {
		return 1;
	}

	if(result->count + 1 >= result->maxcount) {
		if(result_grow(result) != 0) {
			return 1;
		}
	}

	result->list[result->count] = name;
	result->count++;

	return 0;
}

void result_free(struct result_t *result)
{
	size_t i;

	if(!result) {
		return;
	}

	if(result->list) {
		for(i = 0; i < result->count; i++) {
			free(result->list[i]);
		}
		free(result->list);
	}
	free(result->name);
	free(result);
}

static int stringcmp(const void *s1, const void *s2)
{
	return strcmp(*(const char **)s1, *(const char **)s2);
}

int result_print(struct result_t *result)
{
	size_t i;

	if(!result->count) {
		return 0;
	}

	qsort(result->list, result->count, sizeof(char *), stringcmp);

	for(i = 0; i < result->count; i++) {
		printf("%s\n", result->list[i]);
	}

	return result->count;
}

int result_cmp(const void *r1, const void *r2)
{
	struct result_t *result1 = *(struct result_t **)r1;
	struct result_t *result2 = *(struct result_t **)r2;

	return strcmp(result1->name, result2->name);
}

/* vim: set ts=2 sw=2 noet: */
