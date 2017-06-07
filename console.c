#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console.h"

static pthread_mutex_t console_m;

char *logfile = NULL;

bool debug = false;

void console_init() {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&console_m, &attr);
	pthread_mutexattr_destroy(&attr);
}

void console_cleanup() {
	pthread_mutex_destroy(&console_m);
	
	if (logfile) {
		free(logfile);
	}
}

void console_lock() {
	pthread_mutex_lock(&console_m);
}

void console_unlock() {
	pthread_mutex_unlock(&console_m);
}

void console_print(int type, const char *format, ...) {
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

