int match_glob(void *glob, const char *line, int flags);
int match_regex(void *regex, const char *line, int flags);
int match_exact(void *match, const char *line, int flags);
