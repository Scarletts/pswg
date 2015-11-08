/*
 * Copyright (c) 2015 Scarletts <scarlett@entering.space>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "xalloc.h"
#include "util.h"

char *
fdread_fully(int fd, size_t *out_size)
{
	ssize_t ret = 1;
	char *buf = xmalloc(4096);
	size_t bufsize = 4096;
	size_t count = 0;

	while (ret != 0) {
		if (bufsize <= (count + 1024)) {
			bufsize = count + 4096;
			buf = xrealloc(buf, bufsize);
		}
		ret = read(fd, buf + count, 1024);
		if (ret == -1) {
			perror("read");
			goto error;
		}
		count += (size_t)ret;
	}

	if (out_size != NULL) {
		*out_size = count;
	}
	buf[count] = '\0';
	return buf;
error:
	free(buf);
	return NULL;
}

char *
strip_extension(char *filename)
{
	char *p;

	for (p = filename; *p != '\0'; ++p);
	for (; p >= filename; --p) {
		if (*p == '.') {
			*p = '\0';
			break;
		}
	}
	return filename;
}

char *
read_pipe(char *args[], size_t *out_len)
{
	int status = 1;
	int child_pipe[2];
	char *output;
	pid_t pid;

	if (pipe(child_pipe) == -1) {
		perror("pipe");
		return NULL;
	}

	pid = fork();
	switch (pid) {
		case -1:
			perror("fork");
			return NULL;
		case 0:
			close(child_pipe[0]);
			dup2(child_pipe[1], STDOUT_FILENO);
			execvp(args[0], args);
			perror("execvp");
			exit(1);
			break;
	}

	close(child_pipe[1]);
	if (waitpid(pid, &status, 0) == -1) {
		perror("wait");
		return NULL;
	}
	if (status != 0) {
		fprintf(stderr,
		    "read_pipe: child %s process terminated unsuccessfully\n",
		    args[0]);
		return NULL;
	}
	output = fdread_fully(child_pipe[0], out_len);
	close(child_pipe[0]);
	return output;
}

