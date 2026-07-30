/*
 * logger.c
 *
 *  Created on: Apr 7, 2012
 *      Author: abhinavsingh
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "server.h"
#include "conf.h"

void
log_free(logger *log) {
	if (log == NULL) return;
	if (log->fd >= 0 && log->fd != STDERR_FILENO) {
		close(log->fd);
	}
	free(log);
}

int
log_set_file(logger *log, const char *logfile) {
	if (log == NULL) return -1;
	if (log->fd >= 0 && log->fd != STDERR_FILENO) {
		close(log->fd);
	}

	log->logfile = logfile;
	log->fd = logfile == NULL
		? STDERR_FILENO
		: open(logfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
	return log->fd < 0 ? -1 : 0;
}

logger *
log_new(const char *logfile, log_level level) {
	logger *log;
	log = (logger *)calloc(1, sizeof(logger));
	if (log == NULL) return NULL;

	log->self = getpid();
	log->logfile = logfile;
	log->verbosity = level;

	log->fd = -1;
	log_set_file(log, logfile);

	return log;
}

void
log_it(logger *log, log_level level, const char *body) {
	const char *c = ".-*#";

	time_t now;
	char time_buf[64];

	size_t sz;
	char msg[124];

	char line[256];
	int line_sz, ret;

	if(level > log->verbosity) return;
	if(log->fd < 0) return;

	/* limit max log size */
	sz = strlen(body);
	snprintf(msg, sz + 1 > sizeof(msg) ? sizeof(msg) : sz + 1, "%s", body);

	/* time */
	now = time(NULL);
	strftime(time_buf, sizeof(time_buf), "%d %b %H:%M:%S", localtime(&now));

	/* out line */
	line_sz = snprintf(line, sizeof(line), "[%d] %s %d %s\n", (int)log->self, time_buf, c[level], msg);

	/* write to log and flush to disk. */
	ret = write(log->fd, line, line_sz);
	ret = fsync(log->fd);

	(void)ret;
}
