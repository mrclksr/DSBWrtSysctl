/*-
 * Copyright (c) 2015 Marcel Kaiser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>

#define PATH_SYSCTL_CONF "/etc/sysctl.conf"

typedef struct var_s {
	char *var;
	void *val;
	bool written;
} var_t;

static int   nvars = 0;
static var_t *vars = NULL;

static char  *readln(FILE *);
static void  add_var(const char *, const char *);
static void  write_vars(void);
static void  usage(void);

int
main(int argc, char *argv[])
{
	int  ch;
	char *p;

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();
	while (argc-- > 0) {
		p = strchr(*argv, '=');
		if (p == NULL)
			usage();
		*p++ = '\0';
		add_var(*argv++, p);
	}
	write_vars();
	return (EXIT_SUCCESS);
}

static char *
readln(FILE *fp)
{
	static char   *p, *buf = NULL;
	static size_t bsize = 0, slen = 0, rd, len = 0;
	
	for (errno = 0;;) {
		if (bsize == 0 || len == bsize - 1) {
			buf = realloc(buf, bsize + _POSIX2_LINE_MAX);
			if (buf == NULL)
				err(EXIT_FAILURE, "realloc()");
			bsize += _POSIX2_LINE_MAX;
		}
		if (slen > 0)
			(void)memmove(buf, buf + slen, len + 1), slen = 0;
		if (len > 0 && (p = strchr(buf, '\n')) != NULL) {
			slen = p - buf + 1; buf[slen - 1] = '\0'; len -= slen;
			return (buf);
		}
		if ((rd = fread(buf + len, 1, bsize - len - 1, fp)) == 0) {
			if (!ferror(fp) && len > 0) {
				len = 0;
				return (buf);
			} else if (ferror(fp))
				err(EXIT_FAILURE, "fread()");
			return (NULL);
		}
		len += rd; buf[len] = '\0';
	}
}

static void
add_var(const char *var, const char *val)
{

	if (strlen(var) > strcspn(var, "\t\n\r ") || strchr(var, '.') == NULL)
		errx(EXIT_FAILURE, "Error: Invalid sysctl name");
	while (isspace(*val))
		val++;
	if (!*val)
		errx(EXIT_FAILURE, "Error: No value defined for '%s'", var);
	if (sysctlbyname(var, NULL, NULL, NULL, 0) == -1 && errno == ENOENT)
		warnx("Warning: Unknown sysctl name '%s'", var);
	vars = realloc(vars, sizeof(var_t) * (nvars + 1));
	if (vars == NULL)
		err(EXIT_FAILURE, "realloc()");
	if ((vars[nvars].var = strdup(var)) == NULL ||
	    (vars[nvars].val = strdup(val)) == NULL)
		err(EXIT_FAILURE, "strdup()");
	vars[nvars].written = false;
	nvars++;
}

static void
write_vars()
{
	int	    i, fd;
	FILE	    *fp, *tmp;
	char	    *p, *ln, tmpath[] = PATH_SYSCTL_CONF".XXXX";
	bool	    found;
	size_t	    len;
	struct stat sb;

	if (stat(PATH_SYSCTL_CONF, &sb) == -1)
		err(EXIT_FAILURE, "stat(%s)", PATH_SYSCTL_CONF);
	if ((fp = fopen(PATH_SYSCTL_CONF, "r+")) == NULL)
		err(EXIT_FAILURE, "fopen(%s)", PATH_SYSCTL_CONF);
	if (flock(fileno(fp), LOCK_EX) == -1)
		err(EXIT_FAILURE, "flock()");
	if ((fd = mkstemp(tmpath)) == -1)
		err(EXIT_FAILURE, "mkstemp()");
	if ((tmp = fdopen(fd, "w")) == NULL)
                err(EXIT_FAILURE, "fdopen()");
	while ((ln = readln(fp)) != NULL) {
		/* Skip leading white spaces. */
		for (p = ln; *p != '\0' && isspace(*p); p++)
			;
		len = strcspn(p, "=");
		for (i = 0, found = false; !found && i < nvars;) {
			if (len == strlen(vars[i].var) &&
			    strncmp(vars[i].var, p, len) == 0)
				found = true;
			else
				i++;
		}
		if (!found) {
			if (fprintf(tmp, "%s\n", ln) < 0)
				err(EXIT_FAILURE, "fprintf()");
		} else {
			if (fprintf(tmp, "%s=%s\n", vars[i].var,
			    (char *)vars[i].val) < 0)
				err(EXIT_FAILURE, "fprintf()");
			vars[i].written = true;
		}
	}
	if (ferror(fp))
		err(EXIT_FAILURE, "readln()");
	for (i = 0; i < nvars; i++) {
		if (vars[i].written)
			continue;
		if (fprintf(tmp, "%s=%s\n", vars[i].var,
		    (char *)vars[i].val) < 0)
			err(EXIT_FAILURE, "fprintf()");
	}
	if (fclose(tmp) == -1)
		err(EXIT_FAILURE, "fclose()");
	if (chmod(tmpath, sb.st_mode) == -1)
		err(EXIT_FAILURE, "chmod(%s, %u)", tmpath, sb.st_mode);
	if (rename(tmpath, PATH_SYSCTL_CONF) == -1) {
		warn("rename()"); (void)remove(tmpath);
		exit(EXIT_FAILURE);
	}
}

static void
usage()
{
	(void)printf("Usage: %s -h\n" \
	    "       %s var1=val1 var2=val2 ...\n", PROGRAM, PROGRAM);
	exit(EXIT_FAILURE);
}

