#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>

#include "console.h"
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

int view_load(lua_State *L, view_t *v) {
	int n = 0;

	int t = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, t)) {
		resource_t *r = resource_new();
		resource_load(L, r);

		list_add_tail(&r->_l, &v->resources);

		r->index = n++;
	}	

	return n;
}

void view_copy(view_t *v, view_t *w) {
	for (resource_t *i = list_entry(v->resources.prev, typeof(*i), _l), 
	     *j = list_entry(w->resources.prev, typeof(*j), _l);
	     &i->_l != &v->resources && &j->_l != &w->resources;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
		i->status = j->status;
		i->owner = j->owner;
	}
}

void view_merge(view_t *v, view_t *w, bool override) {
	for (resource_t *i = list_entry(v->resources.prev, typeof(*i), _l), 
	     *j = list_entry(w->resources.prev, typeof(*j), _l);
	     &i->_l != &v->resources && &j->_l != &w->resources;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
		switch (i->status) {
			case RESOURCE_STATUS_FREE:
			case RESOURCE_STATUS_TAKEN:
				if (j->status != RESOURCE_STATUS_UNKNOWN && override) {
					i->status = j->status;
					i->owner = j->owner;
				}
				break;

			case RESOURCE_STATUS_UNKNOWN:
				i->status = j->status;
				i->owner = j->owner;
				break;
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

		memcpy(_r, r, sizeof(resource_t));
		_r->type = strdup(r->type);

		list_add_tail(&_r->_l, &_v->resources);
	}
	
	return _v;
}

char * view_to_string(view_t *v) {
	char *string = (char *) calloc(1, 1);

	int tile = -1;
	for_each_entry(resource_t, r, &v->resources) {
		if (tile != r->tile) {
			string = (char *) realloc(string, strlen(string) + 3 + 1);
			strcat(string, "|| ");

			tile = r->tile;
		}
		int tile_digits = r->tile != 0 ? floor(log10(abs(r->tile))) + 1 : 1;
		char *resource = (char *) malloc(strlen(r->type) + tile_digits + 3 + 1);
		sprintf(resource, "%s[%i] ", r->type, r->tile);
		string = (char *) realloc(string, strlen(string) + strlen(resource) + 1);
		strcat(string, resource);

		int owner_digits;
		if (r->status == RESOURCE_STATUS_TAKEN) {
			owner_digits = r->owner != 0 ? floor(log10(abs(r->owner))) + 1 : 1;
		} else if (r->status == RESOURCE_STATUS_FREE || r->status == RESOURCE_STATUS_UNKNOWN) {
			owner_digits = 1;
		}
		int diff = owner_digits + 1 - strlen(resource);
		if (diff > 0) {
			char *blank = (char *) calloc(1, diff + 1);
			memset(blank, ' ', diff);
			string = (char *) realloc(string, strlen(string) + diff + 1);
			strcat(string, blank);
			free(blank);
		}

		free(resource);
	}

	string = (char *) realloc(string, strlen(string) + 2);
	strcat(string, "\n");

	for_each_entry(resource_t, r, &v->resources) {
		if (tile != r->tile) {
			string = (char *) realloc(string, strlen(string) + 3 + 1);
			strcat(string, "|| ");

			tile = r->tile;
		}

		int owner_digits = r->owner != 0 ? floor(log10(abs(r->owner))) + 1 : 1;
		char *owner = (char *) malloc(owner_digits + 1 + 1);
		if (r->status == RESOURCE_STATUS_TAKEN) {
			sprintf(owner, "%i ", r->owner);
		} else if (r->status == RESOURCE_STATUS_FREE) {
			sprintf(owner, "- ");
		} else if (r->status == RESOURCE_STATUS_UNKNOWN) {
			sprintf(owner, "? ");
		}
		string = (char *) realloc(string, strlen(string) + strlen(owner) + 1);
		strcat(string, owner);
		int tile_digits = r->tile != 0 ? floor(log10(abs(r->tile))) + 1 : 1;
		int diff = strlen(r->type) + tile_digits + 3 - strlen(owner);
		if (diff > 0) {
			char *blank = (char *) calloc(1, diff + 1);
			memset(blank, ' ', diff);
			string = (char *) realloc(string, strlen(string) + diff + 1);
			strcat(string, blank);
			free(blank);
		}

		free(owner);
	}

	string = (char *) realloc(string, strlen(string) + 2);
	strcat(string, "\n");

	return string;
}

void view_dump(view_t *v) {
	char *string = view_to_string(v);

	print(string);

	free(string);
}

bool view_compare(view_t *v, view_t *w) {
	for (resource_t *i = list_entry(v->resources.prev, typeof(*i), _l), 
	     *j = list_entry(w->resources.prev, typeof(*j), _l);
	     &i->_l != &v->resources && &j->_l != &w->resources;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
		if (i->status == RESOURCE_STATUS_UNKNOWN || j->status == RESOURCE_STATUS_UNKNOWN) {
			continue;
		} else if (i->status != j->status) {
			return false;
		} else if (i->status == RESOURCE_STATUS_TAKEN && i->owner != j->owner) {
			return false;
		}
	}

	return true;
}

bool view_is_affected(view_t *v, int id, view_t *w) {
	for (resource_t *i = list_entry(v->resources.prev, typeof(*i), _l), 
	     *j = list_entry(w->resources.prev, typeof(*j), _l);
	     &i->_l != &v->resources && &j->_l != &w->resources;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
		if (i->status == RESOURCE_STATUS_TAKEN && i->owner == id) {
			if (j->status == RESOURCE_STATUS_FREE || (j->status == RESOURCE_STATUS_TAKEN && j->owner != id)) {
				return true;
			}
		}
	}

	return false;
}

resource_t * view_get_tile(view_t *v, int tile, int *pos) {
	int i = 0;

	for_each_entry(resource_t, r, &v->resources) {
		if (r->tile == tile) {
			if (pos) {
				*pos = i;
			}

			return r;
		}

		i++;
	}

	return NULL;
}

void view_update(view_t *v, view_t *w) {
	for_each_entry(resource_t, r, &v->resources) {
		for_each_entry(resource_t, _r, &w->resources) {
			if (r->ref == _r->ref) {
				r->status = _r->status;
				r->owner = _r->owner;
				break;
			}
		}
	}
}

int view_count_resources(view_t *v, int id) {
	int n = 0;
	for_each_entry(resource_t, r, &v->resources) {
		if (r->status == RESOURCE_STATUS_TAKEN && r->owner == id) {
			n++;
		}
	}

	return n;
}

resource_t * view_get_resource(view_t *v, int index) {
	for_each_entry(resource_t, r, &v->resources) {
		if (r->index == index) {
			return r;
		}
	}

	return NULL;
}

