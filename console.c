#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "console.h"
#include "dcop.h"

static pthread_mutex_t console_m;

char *logfile = NULL;

bool debug = false;

static FILE *null = NULL;
static int _stdout = -1;

bool silent = false;

void console_init() {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&console_m, &attr);
	pthread_mutexattr_destroy(&attr);

	null = fopen("/dev/null", "w");
}

void console_cleanup() {
	pthread_mutex_destroy(&console_m);
	
	if (logfile) {
		free(logfile);
	}

	fclose(null);
}

void console_lock() {
	if (silent && !pthread_equal(main_tid, pthread_self())) {
		return;
	}

	pthread_mutex_lock(&console_m);
}

void console_unlock() {
	if (silent && !pthread_equal(main_tid, pthread_self())) {
		return;
	}

	pthread_mutex_unlock(&console_m);
}

void console_print(int type, const char *format, ...) {
	if (silent && !pthread_equal(main_tid, pthread_self())) {
		return;
	}

	if (type == CONSOLE_DEBUG && !debug) {
		return;
	}

	FILE *f;
	char *tag;

	switch (type) {
		default:
		case CONSOLE_DEFAULT:
			f = stdout;
			tag = "";
			break;

		case CONSOLE_WARNING:
			f = stderr;
			tag = "WARNING *** ";
			break; 

		case CONSOLE_ERROR:
			f = stderr;
			tag = "ERROR *** ";
			break;

		case CONSOLE_DEBUG:
			f = stderr;
			tag = "DEBUG *** ";
			break;
	}

	char *fmt = malloc(strlen(format) + strlen(tag) + 1);
	sprintf(fmt, "%s%s", tag, format);

	va_list args, _args;
	va_start(args, format);
	va_copy(_args, args);

	console_lock();

	vfprintf(f, fmt, args);

	if (logfile) {
		FILE *log = fopen(logfile, "a");
		if (log) {
			time_t t = time(NULL);
			struct tm tm;
			localtime_r(&t, &tm);

			char ts[512];
			strftime(ts, sizeof(ts), "%c", &tm);

			fprintf(log, "(%s) ", ts);

			vfprintf(log, fmt, _args);

			fclose(log);
		}
	}

	console_unlock();

	va_end(_args);
	va_end(args);

	free(fmt);
}

void console_disable() {
	if (silent && !pthread_equal(main_tid, pthread_self())) {
		return;
	}

	if (!null) {
		return;
	}

	console_lock();

	fflush(stdout);

	_stdout = dup(STDOUT_FILENO);
	if (_stdout == -1) {
		console_unlock();
	} else {
		dup2(fileno(null), STDOUT_FILENO);
	}
}

void console_enable() {
	if (silent && !pthread_equal(main_tid, pthread_self())) {
		return;
	}

	if (_stdout == -1) {
		return;
	}

	fflush(stdout);

	dup2(_stdout, STDOUT_FILENO);

	console_unlock();

	close(_stdout);
	_stdout = -1;
}

