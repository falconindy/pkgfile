#ifndef _NOSR_RESULT_H
#define _NOSR_RESULT_H

struct result_t {
	size_t count;
	size_t maxcount;
	char *name;
	char **list;
};

struct result_t *result_new(char *name, size_t initial_size);
int result_add(struct result_t *result, char *name);
void result_free(struct result_t *result);
void result_print(struct result_t *result);
int result_cmp(const void *r1, const void *r2);

#endif /* _NOSR_RESULT_H */
