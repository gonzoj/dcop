#ifndef VIEW_H_
#define VIEW_H_

#include <stdbool.h>

#include <lua.h>

typedef struct view view_t;

#include "list.h"
#include "resource.h"
#include "tlm.h"

struct view {
	struct list_head resources;
	int size;
	tlm_t *tlm;
};

view_t * view_new();

view_t * view_new_tlm(tlm_t *tlm);

void view_free(view_t *v);

int view_load(lua_State *L, view_t *v);

void view_copy(view_t *v, view_t *w);

void view_merge(view_t *v, view_t *w, bool override);

void view_clear(view_t *v);

view_t * view_clone(view_t *v);

char * view_to_string(view_t *v);

void view_dump(view_t *v);

bool view_compare(view_t *v, view_t *w);

bool view_is_affected(view_t *v, int id, view_t *w);

resource_t * view_get_tile(view_t *v, int tile, int *pos);

void view_update(view_t *v, view_t *w);

int view_count_resources(view_t *v, int id);

resource_t * view_get_resource(view_t *v, int index);

void view_add_resource(view_t *v, resource_t *r);

void view_del_resource(view_t *v, resource_t *r);

view_t * view_concat(view_t *v, view_t *w);

view_t * view_cut(view_t *v, view_t *w);

#endif /* VIEW_H_ */

