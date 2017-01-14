#include "pkgfile.h"
#include "macro.h"
#include "repo.h"
#include "result.h"
#include "cmddb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdbm.h>
#include <archive.h>
#include <archive_entry.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

static int make_pkgfile_path(char *buff, size_t size, const char *reponame) {
  return snprintf(buff, size, CACHEPATH "/%s.files", reponame);
}

static int make_cmddb_path(char *buff, size_t size, const char *reponame) {
  return snprintf(buff, size, CACHEPATH "/%s.commands", reponame);
}

static const char *okmark_key = "/ok";
static const char *okmark_value = "ok";

static void cmddb_add_entry(GDBM_FILE gdbm, char *basename, char *prefix, char *entry)
{
  size_t prefixlen, entrylen;
  datum key, value;
  size_t added_size;
  char *base_dptr;

  prefixlen = strlen(prefix);
  entrylen = strlen(entry);

  key.dptr = basename;
  key.dsize = strlen(basename) + 1;

  value = gdbm_fetch(gdbm, key);
  added_size = prefixlen + entrylen + 2;

  if (value.dptr == NULL) {
    value.dsize = added_size;
    value.dptr = calloc(1, value.dsize);
    base_dptr = value.dptr;
  } else {
    size_t prev_size = value.dsize;
    value.dsize += added_size;
    value.dptr = realloc(value.dptr, value.dsize);
    base_dptr = value.dptr + prev_size;
  }

  if (value.dptr == NULL) {
    fputs("error: failed to allocate memory for datum\n", stderr);
    goto cleanup;
  }

  memset(base_dptr, 0, added_size);
  memcpy(base_dptr, prefix, prefixlen);
  memcpy(base_dptr + prefixlen + 1, entry, entrylen);

  gdbm_store(gdbm, key, value, GDBM_REPLACE);

cleanup:
  if (value.dptr != NULL) {
    free(value.dptr);
  }
}

static char *db_get_str(GDBM_FILE gdbm, const char *k) {
  datum key, value;
  key.dptr = (char *)k;
  key.dsize = strlen(k) + 1;
  value = gdbm_fetch(gdbm, key);
  if (value.dptr != NULL) {
    value.dptr[value.dsize] = '\0';
  }
  return value.dptr;
}

static int db_set_str(GDBM_FILE gdbm, const char *k, const char *v) {
  datum key, value;
  key.dptr = (char *)k;
  key.dsize = strlen(k) + 1;
  value.dptr = (char *)v;
  value.dsize = strlen(v) + 1;
  return gdbm_store(gdbm, key, value, GDBM_REPLACE);
}

static bool cmddb_add_line(GDBM_FILE gdbm, struct config_t *config, struct repo_t *repo, struct pkg_t *pkg, struct memblock_t *bufline) {
  const size_t len = bufline->size;
  char * const buf = bufline->base;
  int prefixlen = 0;
  bool verbosity_backup;
  _cleanup_free_ char *line = NULL;
  char *basename = NULL;

  if (len == 0 || !is_binary(buf, len)) {
    return true;
  }

  verbosity_backup = config->verbose;
  config->verbose = true;
  prefixlen = format_search_result(&line, repo->name, pkg);
  config->verbose = verbosity_backup;
  if (prefixlen < 0) {
    fputs("error: failed to allocate memory for result\n", stderr);
    return false;
  }

  basename = memrchr(buf, '/', len);
  if (basename != NULL) {
    basename += 1;
  } else {
    basename = buf;
  }

  cmddb_add_entry(gdbm, basename, line, buf);

  return true;
}

void cmddb_gen(struct repo_t *repo, struct config_t *config) {
  GDBM_FILE gdbm = NULL;
  char pkgfile_path[FILENAME_MAX];
  char cmddb_path[FILENAME_MAX];
  _cleanup_free_ char *line = NULL;
  struct archive *a = NULL;
  struct archive_entry *e = NULL;
  struct pkg_t pkg;
  struct stat st;
  void *repodata = MAP_FAILED;
  struct archive_line_reader read_buffer = {};

  make_pkgfile_path(pkgfile_path, sizeof(pkgfile_path), repo->name);
  make_cmddb_path(cmddb_path, sizeof(cmddb_path), repo->name);

  a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

  MALLOC(line, MAX_LINE_SIZE, return);

  repo->fd = open(pkgfile_path, O_RDONLY);
  if (repo->fd < 0) {
    /* fail silently if the file doesn't exist */
    if (errno != ENOENT) {
      fprintf(stderr, "error: failed to open repo: %s: %s\n", pkgfile_path,
              strerror(errno));
    }
    goto cleanup;
  }

  fstat(repo->fd, &st);
  repodata = mmap(0, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, repo->fd, 0);
  if (repodata == MAP_FAILED) {
    fprintf(stderr, "error: failed to map pages for %s: %s\n", pkgfile_path,
            strerror(errno));
    goto cleanup;
  }

  if (archive_read_open_memory(a, repodata, st.st_size) != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to load repo: %s: %s\n", pkgfile_path,
            archive_error_string(a));
    goto cleanup;
  }

  gdbm = gdbm_open(cmddb_path, 4096, GDBM_NEWDB, 0755, NULL);
  if (gdbm == NULL) {
    fprintf(stderr, "error: gdbm: %s\n", gdbm_strerror(gdbm_errno));
    goto cleanup;
  }

  while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
    const char *entryname = archive_entry_pathname(e);
    size_t len;
    int r;

    if (entryname == NULL) {
      /* libarchive error */
      continue;
    }

    len = strlen(entryname);
    r = parse_pkgname(&pkg, entryname, len);
    if (r < 0) {
      fprintf(stderr, "error parsing pkgname from: %s: %s\n", entryname,
              strerror(-r));
      continue;
    }

    memset(&read_buffer, 0, sizeof(struct archive_line_reader));
    read_buffer.line.base = line;

    while (reader_getline(&read_buffer, a) == ARCHIVE_OK) {
      cmddb_add_line(gdbm, config, repo, &pkg, &read_buffer.line);
    }
  }

  archive_read_close(a);

  db_set_str(gdbm, okmark_key, okmark_value);

cleanup:
  if (gdbm != NULL) {
    gdbm_close(gdbm);
  }
  archive_read_free(a);
  if (repo->fd >= 0) {
    close(repo->fd);
  }
  if (repodata != MAP_FAILED) {
    munmap(repodata, st.st_size);
  }
}

void cmddb_genall(struct repovec_t *repos, struct config_t *config) {
  struct repo_t *repo;
  REPOVEC_FOREACH(repo, repos) {
    if (!cmddb_valid(repo->name)) {
      printf("* %s\n", repo->name);
      cmddb_gen(repo, config);
    } else {
      printf("  %s is up to date\n", repo->name);
    }
  }
}

bool cmddb_valid(const char *reponame) {
  struct stat pkgfile_stat, cmddb_stat;
  char pkgfile_path[FILENAME_MAX], cmddb_path[FILENAME_MAX];
  GDBM_FILE gdbm;
  char *okmark;

  make_pkgfile_path(pkgfile_path, sizeof(pkgfile_path), reponame);
  make_cmddb_path(cmddb_path, sizeof(cmddb_path), reponame);

  if (stat(pkgfile_path, &pkgfile_stat) != 0) return false;
  if (stat(cmddb_path, &cmddb_stat) != 0) return false;
  if (pkgfile_stat.st_mtim.tv_sec > cmddb_stat.st_mtim.tv_sec) return false;
  if (pkgfile_stat.st_mtim.tv_sec == cmddb_stat.st_mtim.tv_sec
      && pkgfile_stat.st_mtim.tv_nsec >= cmddb_stat.st_mtim.tv_nsec) return false;

  gdbm = gdbm_open(cmddb_path, 4096, 0755, GDBM_READER, NULL);
  if (gdbm == NULL) return false;
  okmark = db_get_str(gdbm, okmark_key);
  if (okmark == NULL || strcmp(okmark, okmark_value) != 0) return false;
  gdbm_close(gdbm);

  return true;
}

struct result_t *cmddb_search(struct result_t *result, const char *reponame, const char *cmd) {
  GDBM_FILE gdbm;
  char cmddb_path[FILENAME_MAX];
  datum key, value;

  make_cmddb_path(cmddb_path, sizeof(cmddb_path), reponame);

  gdbm = gdbm_open(cmddb_path, 4096, 0755, GDBM_READER, NULL);
  if (gdbm == NULL) {
    fprintf(stderr, "error: gdbm: %s\n", gdbm_strerror(gdbm_errno));
    return NULL;
  }

  key.dptr = (char *)cmd;
  key.dsize = strlen(cmd) + 1;
  value = gdbm_fetch(gdbm, key);
  if (value.dptr != NULL) {
    char *repo = NULL, *entry = NULL;
    char *data_end = value.dptr + value.dsize;
    for (char *sb = value.dptr, *se = value.dptr; se < data_end; ++se) {
      if (*se != '\0') continue;
      if (repo != NULL) {
        entry = sb;
        result_add(result, repo, entry, strlen(repo));
        repo = entry = NULL;
      } else {
        repo = sb;
      }
      sb = se + 1;
    }
  }

  gdbm_close(gdbm);

  return result;
}

struct result_t **cmddb_search_all(struct repovec_t *repos, const char *cmd) {
  struct result_t **results;
  struct repo_t *repo;

  results = calloc(sizeof (struct result_t *), repos->size);
  if (results == NULL) {
    fputs("error: failed to allocate sufficient memory for results", stderr);
    return NULL;
  }

  REPOVEC_FOREACH(repo, repos) {
    struct result_t *result;
    results[i_] = result_new(repo->name, 50);
    result = cmddb_search(results[i_], repo->name, cmd);
    if (result == NULL) {
      free(results[i_]);
      fprintf(stderr, "error: failed to search in %s\n", repo->name);
    }
    results[i_] = result;
  }

  return results;
}
