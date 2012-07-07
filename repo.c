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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "repo.h"
#include "util.h"

struct repo_t *repo_new(const char *reponame)
{
	struct repo_t *repo;

	CALLOC(repo, 1, sizeof(struct repo_t), return NULL);

	if(asprintf(&repo->name, "%s", reponame) == -1) {
		fprintf(stderr, "error: failed to allocate memory\n");
		free(repo);
		return NULL;
	}

	/* assume glorious failure */
	repo->err = 1;

	return repo;
}

void repo_free(struct repo_t *repo)
{
	size_t i;

	free(repo->name);
	for(i = 0; i < repo->servercount; i++) {
		free(repo->servers[i]);
	}
	free(repo->servers);

	free(repo->data);
	free(repo->url);

	free(repo);
}

int repo_add_server(struct repo_t *repo, const char *server)
{
	if(!repo) {
		return 1;
	}

	repo->servers = realloc(repo->servers,
			sizeof(char *) * (repo->servercount + 1));

	repo->servers[repo->servercount] = strdup(server);
	repo->servercount++;

	return 0;
}

/* vim: set ts=2 sw=2 noet: */
