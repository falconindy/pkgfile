
#define ALLOC_FAIL(s) do { fprintf(stderr, "alloc failure: could not allocate %zd bytes\n", s); } while(0)
#define MALLOC(p, s, action) do { p = calloc(1, s); if(p == NULL) { ALLOC_FAIL(s); action; } } while(0)
#define CALLOC(p, l, s, action) do { p = calloc(l, s); if(p == NULL) { ALLOC_FAIL(s); action; } } while(0)
#define FREE(p) do { free(p); p = NULL; } while(0)
#define UNUSED __attribute__((unused))

