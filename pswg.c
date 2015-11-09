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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <ftw.h>
#include <pwd.h>

#include "xalloc.h"
#include "util.h"

struct page {
	char *htpath;
	char *title;
	char *body;
	size_t body_len;
	time_t created;
	char *created_iso;
	char *created_readable;
	time_t modified;
	char *modified_iso;
	char *modified_readable;
	char *user;
};

struct context {
	const char *program;
	const char *base_url;
	const char *parser;
	const char *feed_title;
	struct page *pages;
	size_t page_bufsize;
	size_t page_count;
	bool hide_user;
} x = {0};


static void
free_page(struct page *p)
{
	if (p == NULL) return;

	free(p->title);
	free(p->body);
	free(p->user);
	free(p->created_iso);
	free(p->created_readable);
	free(p->modified_iso);
	free(p->modified_readable);
	free(p->htpath);
}

static struct page *
create_page(const char *path, const struct stat *s)
{
	int datefd = -1;
	char *datepath = NULL;
	int ret = 0;
	time_t tsecs;
	struct tm tm;
	char *parser_args[3] = {NULL};
	char timestr[32];
	struct passwd *pw;
	struct page page = {0};

	if (strcmp(path, "index") == 0 ||
	    strncmp(path, "index.", sizeof("index.") - 1) == 0) {
		/* We are looking at the root index. */

		page.title = xstrdup("Home");
	} else {
		const char *p;
		char *t;

		/* Prepare the title from the last part of the path */

		for (p = path; *p != '\0'; ++p);
		for (; p >= path && *(p - 1) != '/'; --p);
		
		/*
		 * If the file is named "index", its title should be the name
		 * of its directory, unless it is the root index.
		 */

		if (strcmp(p, "index") == 0 ||
		    strncmp(p, "index.", sizeof("index.") - 1) == 0) {
			for (--p; p >= path && *(p - 1) != '/'; --p);
			page.title = xstrdup(p);
			for (t = page.title; *t != '\0'; ++t) {
				if (*t == '/') {
					*t = '\0';
					break;
				}
			}
		} else {
			page.title = xstrdup(p);

			/* Strip the file extension and add whitespace */

			for (t = page.title; *t != '\0'; ++t);
			for (; t >= page.title; --t) {
				if (*t == '.') {
					*t = '\0';
					break;
				}
			}
		}

		for (t = page.title; *t != '\0'; ++t) {
			if (*t == '_' || *t == '-') *t = ' ';
		}
	}

	/* we use .date files to track "creation" dates */

	xasprintf(&datepath, "%s.date", path - sizeof("./src/") + 1);

	if ((datefd = open(datepath, O_RDONLY)) == -1) {
		tsecs = time(NULL);
		if ((datefd = open(datepath,
		    O_WRONLY | O_CREAT | O_TRUNC,
		    S_IRUSR | S_IRGRP | S_IROTH)) == -1) {
			perror("open");
			goto error;
		}
	} else {
		struct stat datestat;

		if (fstat(datefd, &datestat) == -1) {
			perror("fstat");
			goto error;
		}
		memcpy(&tsecs, &datestat.st_mtim, sizeof(time_t));
	}

	if (gmtime_r(&tsecs, &tm) == NULL) {
		perror("gmtime_r");
		goto error;
	}

	page.created = tsecs;
	strftime(timestr, sizeof(timestr), "%FT%H:%M:%SZ", &tm);
	page.created_iso = xstrdup(timestr);
	strftime(timestr, sizeof(timestr), "%F %H:%M UTC", &tm);
	page.created_readable = xstrdup(timestr);

	if (gmtime_r((time_t *)&s->st_mtim, &tm) == NULL) {
		perror("gmtime_r");
		goto error;
	}

	memcpy(&page.modified, &s->st_mtim, sizeof(time_t));
	strftime(timestr, sizeof(timestr), "%FT%H:%M:%SZ", &tm);
	page.modified_iso = xstrdup(timestr);
	strftime(timestr, sizeof(timestr), "%F %H:%M UTC", &tm);
	page.modified_readable = xstrdup(timestr);

	pw = getpwuid(s->st_uid);
	page.user = xstrdup(pw != NULL ? pw->pw_name : "NULL");

	parser_args[0] = xstrdup(x.parser);
	parser_args[1] = xstrdup(path - sizeof("./src/") + 1);

	if ((page.body = read_pipe(parser_args, &page.body_len)) == NULL) {
		goto error;
	}

	if (x.page_bufsize == 0) {
		x.page_bufsize = 16;
		x.pages = xreallocarray(NULL,
		    x.page_bufsize, sizeof(struct page));
	} else if (x.page_count >= x.page_bufsize) {
		x.page_bufsize = x.page_bufsize * 2;
		x.pages = xreallocarray(x.pages,
		    x.page_bufsize, sizeof(struct page));
	}

	memcpy(x.pages + x.page_count, &page, sizeof(struct page));

end:
	if (datepath != NULL) {
		free(datepath);
	}
	if (datefd != -1) {
		close(datefd);
	}
	free(parser_args[0]);
	free(parser_args[1]);
	return ret == 0 ? &x.pages[x.page_count++] : NULL;
error:
	ret = -1;
	goto end;
}

static int
traverse(const char *path, const struct stat *s, int flag)
{
	int ret = 0;
	char *path_no_ext = NULL;
	FILE *out = NULL;
	char *out_path = NULL;
	char *header = NULL;
	size_t header_len;
	char *footer = NULL;
	size_t footer_len;
	char *sed_args[16] = {NULL};
	struct page *page = NULL;
	const char *date_ext = path;

	while ((date_ext = strstr(date_ext, ".date")) != NULL) {
		if (*(date_ext + sizeof(".date") - 1) == '\0') {
			return 0;
		}
	}

	/* Strip the first part of the path to get the HTTP path */

	path += sizeof("./src") - 1;
	if (path[0] == '\0') return 0;
	++path;

	if (S_ISDIR(s->st_mode)) {
		puts(path);

		xasprintf(&out_path, "./build/%s", path);

		if (mkdir(out_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
			if (errno != EEXIST) {
				perror("mkdir");
				goto error;
			}
		}
	} else {
		time_t secs = time(NULL);
		struct tm now;

		if (gmtime_r(&secs, &now) == NULL) {
			perror("gmtime_r");
			goto error;
		}

		if ((page = create_page(path, s)) == NULL) {
			goto error;
		}

		path_no_ext = strip_extension(xstrdup(path));
		xasprintf(&out_path, "./build/%s.html", path_no_ext);

		page->htpath = xstrdup(out_path + sizeof("./build") - 1);

		printf("%s -> %s (%s)\n", path, page->title, out_path);

		/* Make header */

		sed_args[0] = xstrdup("sed");

		xasprintf(&sed_args[1], "-e s|${base_url}|%s|g",
		    x.base_url);
		xasprintf(&sed_args[2], "-e s|${year}|%d|g",
		    now.tm_year + 1900);
		xasprintf(&sed_args[3], "-e s|${created}|%s|g",
		    page->created_iso);
		xasprintf(&sed_args[4], "-e s|${created_readable}|%s|g",
		    page->created_readable);
		xasprintf(&sed_args[5], "-e s|${modified}|%s|g",
		    page->modified_iso);
		xasprintf(&sed_args[6], "-e s|${modified_readable}|%s|g",
		    page->modified_readable);
		xasprintf(&sed_args[7], "-e s|${owner}|%s|g", page->user);
		xasprintf(&sed_args[8], "-e s|${title}|%s|g", page->title);

		sed_args[9] = xstrdup("header.html");

		if ((header = read_pipe(sed_args, &header_len)) == NULL) {
			goto error;
		}

		free(sed_args[9]);
		sed_args[9] = xstrdup("footer.html");
		if ((footer = read_pipe(sed_args, &footer_len)) == NULL) {
			goto error;
		}

		out = fopen(out_path, "w");
		if (out == NULL) {
			perror("fopen");
			goto error;
		}
		if (fwrite(header, 1, header_len, out) < header_len) {
			perror("fwrite");
			goto error;
		}
		if (fwrite(page->body, 1, page->body_len, out) < page->body_len) {
			perror("fwrite");
			goto error;
		}
		if (fwrite(footer, 1, footer_len, out) < footer_len) {
			perror("fwrite");
			goto error;
		}
	}

end:
	if (out != NULL) {
		fclose(out);
	}
	free(out_path);
	free(header);
	free(footer);
	free(path_no_ext);
	for (size_t i = 0; i < (sizeof(sed_args) / sizeof(char *)); ++i) {
		free(sed_args[i]);
	}
	return ret;
error:
	ret = -1;
	goto end;
}

static int
create_archive(void)
{
	int ret = 0;
	char *sed_args[16] = {NULL};
	FILE *out = NULL;
	char *header = NULL;
	size_t header_len;
	char *footer = NULL;
	size_t footer_len;
	time_t secs = time(NULL);
	char timestr[32];
	struct tm now;

	if (gmtime_r(&secs, &now) == NULL) {
		perror("gmtime_r");
		goto error;
	}

	sed_args[0] = xstrdup("sed");

	xasprintf(&sed_args[1], "-e s|${base_url}|%s|g", x.base_url);
	xasprintf(&sed_args[2], "-e s|${year}|%d|g", now.tm_year + 1900);
	strftime(timestr, sizeof(timestr), "%FT%H:%M:%SZ", &now);
	xasprintf(&sed_args[3], "-e s|${created}|%s|g", timestr);
	xasprintf(&sed_args[4], "-e s|${modified}|%s|g", timestr);
	strftime(timestr, sizeof(timestr), "%F %H:%M UTC", &now);
	xasprintf(&sed_args[5], "-e s|${created_readable}|%s|g", timestr);
	xasprintf(&sed_args[6], "-e s|${modified_readable}|%s|g", timestr);
	xasprintf(&sed_args[7], "-e s|${owner}|%s|g", getlogin());
	xasprintf(&sed_args[8], "-e s|${title}|%s|g", "Archive");

	sed_args[9] = xstrdup("header.html");

	if ((header = read_pipe(sed_args, &header_len)) == NULL) {
		goto error;
	}

	free(sed_args[9]);
	sed_args[9] = xstrdup("footer.html");
	if ((footer = read_pipe(sed_args, &footer_len)) == NULL) {
		goto error;
	}

	out = fopen("./build/archive.html", "w");
	if (out == NULL) {
		perror("fopen");
		goto error;
	}

	if (fwrite(header, 1, header_len, out) < header_len) {
		perror("fwrite");
		goto error;
	}

	if (fputs("<h1>Archive</h1>\n", out) < 0) goto efputs;
	if (fputs("<table class=\"sortable archive\">\n", out) < 0) {
		goto efputs;
	}
	if (fputs("<thead>\n", out) < 0) goto efputs;
	if (fputs("<tr>\n", out) < 0) goto efputs;
	if (fputs("<th>Title</th>\n", out) < 0) goto efputs;
	if (fputs("<th>Date created</th>\n", out) < 0) goto efputs;
	if (fputs("<th>Date modified</th>\n", out) < 0) goto efputs;
	if (!x.hide_user) {
		if (fputs("<th>Author</th>\n", out) < 0) goto efputs;
	}
	if (fputs("</tr>\n", out) < 0) goto efputs;
	if (fputs("</thead>\n", out) < 0) goto efputs;
	if (fputs("<tbody>\n", out) < 0) goto efputs;

	for (size_t i = 0; i < x.page_count; ++i) {
		struct page *p = &x.pages[i];

		if (fputs("<tr>\n", out) < 0) goto efputs;
		if (fprintf(out, "<td><a href=\"%s%s\">%s</a></td>\n",
		    x.base_url, p->htpath, p->title) < 0) {
			goto efprintf;
		}
		if (fprintf(out, "<td><date datetime=\"%s\">%s</date></td>\n",
		    p->created_iso, p->created_readable) < 0) {
			goto efprintf;
		}
		if (fprintf(out, "<td><date datetime=\"%s\">%s</date></td>\n",
		    p->modified_iso, p->modified_readable) < 0) {
			goto efprintf;
		}
		if (!x.hide_user &&
		    fprintf(out, "<td>%s</td>\n", p->user) < 0) {
			goto efprintf;
		}
		if (fputs("</tr>\n", out) < 0) goto efputs;
	}

	if (fputs("</tbody>\n", out) < 0) goto efputs;
	if (fputs("</table>\n", out) < 0) goto efputs;
	if (fputs("<p>This table should be sortable (by selecting the headers)"
	    " with a JavaScript-capable user agent.</p> \n", out) < 0) {
		goto efputs;
	}

	if (fwrite(footer, 1, footer_len, out) < footer_len) {
		perror("fwrite");
		goto error;
	}

end:
	if (out != NULL) {
		fclose(out);
	}
	for (size_t i = 0; i < (sizeof(sed_args) / sizeof(char *)); ++i) {
		free(sed_args[i]);
	}
	return ret;
efprintf:
	perror("fprintf");
	goto error;
efputs:
	perror("fputs");
	goto error;
error:
	ret = -1;
	goto end;
}

static int
create_news(const char *filename)
{
	int ret = 0;
	char *sed_args[16] = {NULL};
	FILE *out = NULL;
	char *header = NULL;
	size_t header_len;
	char *footer = NULL;
	size_t footer_len;
	time_t secs = time(NULL);
	char timestr[32];
	struct tm now;
	size_t count = x.page_count < 10 ? x.page_count : 10;

	if (gmtime_r(&secs, &now) == NULL) {
		perror("gmtime_r");
		goto error;
	}

	sed_args[0] = xstrdup("sed");

	xasprintf(&sed_args[1], "-e s|${base_url}|%s|g", x.base_url);
	xasprintf(&sed_args[2], "-e s|${year}|%d|g", now.tm_year + 1900);
	strftime(timestr, sizeof(timestr), "%FT%H:%M:%SZ", &now);
	xasprintf(&sed_args[3], "-e s|${created}|%s|g", timestr);
	xasprintf(&sed_args[4], "-e s|${modified}|%s|g", timestr);
	strftime(timestr, sizeof(timestr), "%F %H:%M UTC", &now);
	xasprintf(&sed_args[5], "-e s|${created_readable}|%s|g", timestr);
	xasprintf(&sed_args[6], "-e s|${modified_readable}|%s|g", timestr);
	xasprintf(&sed_args[7], "-e s|${owner}|%s|g", getlogin());
	xasprintf(&sed_args[8], "-e s|${title}|%s|g", "News");

	sed_args[9] = xstrdup("header.html");

	if ((header = read_pipe(sed_args, &header_len)) == NULL) {
		goto error;
	}

	free(sed_args[9]);
	sed_args[9] = xstrdup("footer.html");
	if ((footer = read_pipe(sed_args, &footer_len)) == NULL) {
		goto error;
	}

	out = fopen(filename, "w");
	if (out == NULL) {
		perror("fopen");
		goto error;
	}

	if (fwrite(header, 1, header_len, out) < header_len) {
		perror("fwrite");
		goto error;
	}

	for (size_t i = 0; i < count; ++i) {
		struct page *p = &x.pages[i];
		bool multi_paragraph = false;
		char *str = xstrdup(p->body);
		char *body = str;
		char *endpara;

		/* don't include the initial header tag in the body */
		if (strncmp("<h", body, 2) == 0) {
			char *past_header = strstr(body + 2, "</h");

			if (past_header != NULL) {
				body = past_header + 5;
			}
		}

		/* terminate after the end of the first paragraph */
		if ((endpara = strstr(body, "</p>")) != NULL) {
			if (strstr(endpara + 4, "<p>") != NULL) {
				multi_paragraph = true;
			}
			*(endpara + 4) = '\0';
		}

		if (fputs("<article class=\"preview\">\n", out) < 0) {
			free(str);
			goto efputs;
		}
		if (fprintf(out, "<h2><a href=\"%s%s\">%s</a></h2>\n",
		    x.base_url, p->htpath, p->title) < 0) {
			free(str);
			goto efprintf;
		}
		if (fprintf(out, "<p class=\"byline\">Created "
		    "<date datetime=\"%s\">%s</date>",
		    p->created_iso, p->created_readable) < 0) {
			free(str);
			goto efprintf;
		}
		if (!x.hide_user && fprintf(out, " by %s", p->user) < 0) {
			free(str);
			goto efprintf;
		}
		if (fputs("</p>\n", out) < 0) {
			free(str);
			goto efputs;
		}
		if (fputs(body, out) < 0) goto efputs;
		free(str);
		if (multi_paragraph) {
			if (fputs("\n<p class=\"cont\"><em>"
			    "Continued...</em></p>", out) < 0) {
				goto efputs;
			}
		}
		if (fputs("\n</article>\n", out) < 0) goto efputs;
	}

	if (fwrite(footer, 1, footer_len, out) < footer_len) {
		perror("fwrite");
		goto error;
	}

end:
	if (out != NULL) {
		fclose(out);
	}
	for (size_t i = 0; i < (sizeof(sed_args) / sizeof(char *)); ++i) {
		free(sed_args[i]);
	}
	return ret;
efprintf:
	perror("fprintf");
	goto error;
efputs:
	perror("fputs");
	goto error;
error:
	ret = -1;
	goto end;
}

static int
create_feed(void)
{
	int ret = 0;
	FILE *out = NULL;
	time_t secs = time(NULL);
	struct tm now;
	char timestr[32];
	size_t count = x.page_count < 20 ? x.page_count : 20;

	if (gmtime_r(&secs, &now) == NULL) {
		perror("gmtime_r");
		goto error;
	}

	strftime(timestr, sizeof(timestr), "%FT%H:%M:%SZ", &now);

	out = fopen("./build/atom.xml", "w");
	if (out == NULL) {
		perror("fopen");
		goto error;
	}

	if (fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", out) < 0) {
		goto efputs;
	}
	if (fputs("<feed xmlns=\"http://www.w3.org/2005/Atom\">\n", out) < 0) {
		goto efputs;
	}
	if (fprintf(out, "<title>%s</title>\n", x.feed_title) < 0) {
		goto efprintf;
	}
	if (fprintf(out, "<id>%s</id>\n", x.base_url) < 0) {
		goto efprintf;
	}
	if (fprintf(out, "<link href=\"%s/\" />\n", x.base_url) < 0) {
		goto efprintf;
	}
	if (fprintf(out, "<link rel=\"self\" href=\"%s/atom.xml\" />\n",
	    x.base_url) < 0) {
		goto efprintf;
	}
	if (!x.hide_user) {
		if (fputs("<author>\n", out) < 0) goto efputs;
		if (fprintf(out, "\t<name>%s</name>\n", getlogin()) < 0) {
			goto efprintf;
		}
		if (fputs("</author>\n", out) < 0) goto efputs;
	}
	if (fprintf(out, "<updated>%s</updated>\n", timestr) < 0) {
		goto efprintf;
	}

	for (size_t i = 0; i < count; ++i) {
		struct page *p = &x.pages[i];

		if (fputs("\n<entry>\n", out) < 0) goto efputs;
		if (fprintf(out, "<title>%s</title>\n", p->title) < 0) {
			goto efprintf;
		}
		if (fprintf(out, "<link href=\"%s%s\" />\n",
		    x.base_url, p->htpath) < 0) {
			goto efprintf;
		}
		if (fprintf(out, "<id>%s%s</id>\n",
		    x.base_url, p->htpath) < 0) {
			goto efprintf;
		}
		if (!x.hide_user) {
			if (fputs("<author>\n", out) < 0) goto efputs;
			if (fprintf(out, "\t<name>%s</name>\n", p->user) < 0) {
				goto efprintf;
			}
			if (fputs("</author>\n", out) < 0) goto efputs;
		}
		if (fprintf(out, "<published>%s</published>\n",
		    p->created_iso) < 0) {
			goto efprintf;
		}
		if (fprintf(out, "<updated>%s</updated>\n",
		    p->modified_iso) < 0) {
			goto efprintf;
		}
		if (fputs("\n<content type=\"html\">\n", out) < 0) {
			goto efputs;
		}
		if (fputs(p->body, out) < 0) {
			goto efputs;
		}
		if (fputs("</content>\n", out) < 0) goto efputs;
		if (fputs("</entry>\n", out) < 0) goto efputs;
	}

	if (fputs("</feed>\n", out) < 0) goto efputs;

end:
	fclose(out);
	return ret;
efprintf:
	perror("fprintf");
	goto error;
efputs:
	perror("fputs");
	goto error;
error:
	ret = -1;
	goto end;
}

static int
compare_page_dates(const void *v1, const void *v2)
{
	const struct page *p1 = v1;
	const struct page *p2 = v2;

	if (p1->created > p2->created) {
		return -1;
	}
	if (p1->created < p2->created) {
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int ch;
	int ret = 0;
	bool archived = false;
	bool syndicated = false;
	bool make_news = false;
	bool news_is_home = false;

	x.program = argv[0];
	x.base_url = "";
	x.parser = "cat";

	while ((ch = getopt(argc, argv, "ab:fhnp:t:u")) != -1) {
		switch (ch) {
			case 'a':
				archived = true;
				break;
			case 'b':
				x.base_url = optarg;
				break;
			case 'f':
				syndicated = true;
				break;
			case 'h':
				make_news = true;
				news_is_home = true;
				break;
			case 'n':
				make_news = true;
				news_is_home = false;
				break;
			case 'p':
				x.parser = optarg;
				break;
			case 't':
				x.feed_title = optarg;
				break;
			case 'u':
				x.hide_user = true;
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (mkdir("./build", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
		if (errno != EEXIST) {
			perror("mkdir");
			goto error;
		}
	}

	if (ftw("./src", traverse, 8) == -1) {
		perror("ftw");
		goto error;
	}

	if (archived) {
		puts("Building archive...");
		if (create_archive() == -1) goto error;
	}

	qsort(x.pages, x.page_count, sizeof(struct page),
	    compare_page_dates);

	if (syndicated) {
		puts("Building feed...");
		if (x.feed_title == NULL) {
			fprintf(stderr,
			    "%s: no feed title specified, use -t title\n",
			    x.program);
			goto error;
		}
		if (create_feed() == -1) goto error;
	}

	if (make_news) {
		puts("Building news...");
		if (create_news(news_is_home ?
		    "./build/index.html" : "./build/news.html") == -1) {
			goto error;
		}
	}
end:
	for (size_t i = 0; i < x.page_count; ++i) {
		free_page(&x.pages[i]);
	}
	free(x.pages);
	return ret;
error:
	ret = 1;
	goto end;
}

