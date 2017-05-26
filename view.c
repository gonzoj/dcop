#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>

#include "dcop.h"
#include "list.h"
#include "resource.h"
#include "view.h"

view_t * view_new() {
	view_t *v = (view_t *) calloc(1, sizeof(view_t));
	INIT_LIST_HEAD(&v->resources);

	return v;
}

void view_free(view_t *v) {
	if (v) {
		for_each_entry_safe(resource_t, r, _r, &v->resources) {
			list_del(&r->_l);
			resource_free(r);
		}

		free(v);
	}
}

int view_load(dcop_t *dcop, view_t *v) {
	int n = 0;
	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		resource_t *r = resource_new();
		resource_load(dcop, r);
		list_add_tail(&r->_l, &v->resources);
		n++;
	}	
	return n;
}

void view_merge(view_t *v, view_t *w) {
	for (resource_t *i = list_entry(v->resources.prev, typeof(*i), _l), 
	     *j = list_entry(w->resources.prev, typeof(*j), _l);
	     &i->_l != &v->resources && &j->_l != &w->resources;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
	     		if (j->status == RESOURCE_STATUS_TAKEN) {
				i->status = j->status;
				i->owner = j->owner;
			}
	     }
}

void view_clear(view_t *v) {
	for_each_entry(resource_t, r, &v->resources) {
		r->status = RESOURCE_STATUS_UNKNOWN;
	}
}

view_t * view_clone(view_t *v) {
	view_t *_v = view_new();
	for_each_entry(resource_t, r, &v->resources) {
		resource_t *_r = resource_new();
		_r->status = r->status;
		_r->owner = r->owner;
		_r->tile = r->tile;
		_r->type = strdup(r->type);
		_r->ref = r->ref;
		list_add_tail(&_r->_l, &_v->resources);
	}
	
	return _v;
}

void view_dump(view_t *v) {
	//int i = 0;
	for_each_entry(resource_t, r, &v->resources) {
		//printf("%i [%i] %s: %i (%i)\n", ++i, r->tile, r->type, r->status, r->owner);
		if (r->status == RESOURCE_STATUS_TAKEN) {
			printf("%i ", r->owner);
		} else {
			printf("- ");
		}
	}
	printf("\n");
}

