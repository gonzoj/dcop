#ifndef ALGORITHM_H_
#define ALGORITHM_H_

#include "dcop.h"
#include "list.h"

typedef struct algorithm {
	struct list_head _l;
	const char *name;
	void (*init)(dcop_t *dcop, int argc, char **argv);
	void (*cleanup)(dcop_t *dcop);
	void (*run)(dcop_t *dcop);
	void (*kill)(dcop_t *dcop);
} algorithm_t;

#define algorithm_new(n, i, c, r, k) (algorithm_t) { .name = n, .init = i, .cleanup = c, .run = r, .kill = k }

#endif /* ALGORITHM_H_ */

