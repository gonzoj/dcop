#ifndef RESOURCE_H_
#define RESOURCE_H_

#include <stdlib.h>

#include "agent.h"
#include "dcop.h"
#include "list.h"

typedef struct resource {
	struct list_head _l;
	int ref;
	char *type;
	enum {
		RESOURCE_STATUS_UNKNOWN,
		RESOURCE_STATUS_FREE,
		RESOURCE_STATUS_TAKEN
	} status;
	int owner;
	int tile;
} resource_t;

#define resource_new() (resource_t *) calloc(1, sizeof(resource_t))

void resource_free(resource_t *r);

void resource_load(dcop_t *dcop, resource_t *r);

void resource_refresh(dcop_t *dcop, resource_t *r);

#endif /* RESOURCE_H_ */

