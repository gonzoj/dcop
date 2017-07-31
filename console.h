#ifndef CONSOLE_H_
#define CONSOLE_H_

#include <stdbool.h>

enum {
	CONSOLE_DEFAULT,
	CONSOLE_WARNING,
	CONSOLE_ERROR,
	CONSOLE_DEBUG
};

extern char *logfile;

extern bool debug;

extern bool silent;

#define DEBUG if (debug)

void console_init();

void console_cleanup();

void console_lock();

void console_unlock();

void console_print(int, const char *, ...);

#define print(f, a...) console_print(CONSOLE_DEFAULT, f, ## a)
#define print_warning(f, a...) console_print(CONSOLE_WARNING, f, ## a)
#define print_error(f, a...) console_print(CONSOLE_ERROR, f, ## a)
#define print_debug(f, a...) console_print(CONSOLE_DEBUG, f, ## a)

void console_disable();

void console_enable();

#endif /* CONSOLE_H_ */

