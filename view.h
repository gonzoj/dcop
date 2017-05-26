#ifndef VIEW_H_
#define VIEW_H_

#include "dcop.h"
#include "list.h"

typedef struct view {
	struct list_head resources;
} view_t;

view_t * view_new();

void view_free(view_t *v);

int view_load(dcop_t *dcop, view_t *v);

void view_merge(view_t *v, view_t *w);

void view_clear(view_t *v);

view_t * view_clone(view_t *v);

void view_dump(view_t *v);

#endif /* VIEW_H_ */

