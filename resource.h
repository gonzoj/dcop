#ifndef RESOURCE_H_
#define RESOURCE_H_

#include <stdlib.h>

#include <lua.h>

typedef struct resource resource_t;

#include "list.h"
#include "tlm.h"

struct resource {
	struct list_head _l;
	int ref;
	char *type;
	enum {
		RESOURCE_STATUS_UNKNOWN = -1,
		RESOURCE_STATUS_FREE = 0,
		RESOURCE_STATUS_TAKEN = 1
	} status;
	int owner;
	int tile;
	int index;
	tlm_t *tlm;
};

#define resource_new() (resource_t *) calloc(1, sizeof(resource_t))

resource_t * resource_new_tlm(tlm_t *tlm);

void resource_free(resource_t *r);

void resource_load(lua_State *L, resource_t *r);

void resource_refresh(lua_State *L, resource_t *r);

resource_t * resource_clone(resource_t *r);

#define resource_is_free(r) (r->status == RESOURCE_STATUS_FREE)

#define resource_get_owner(r) (r->status == RESOURCE_STATUS_TAKEN ? r->owner : -1)

#endif /* RESOURCE_H_ */

