#ifndef HARDWARE_H_
#define HARDWARE_H_

#include <stdlib.h>

#include <lua.h>

typedef struct hardware hardware_t;

#include "view.h"

struct hardware {
	int number_of_tiles;
	int number_of_resources;
	view_t *view;
};

#define hardware_new() (hardware_t *) calloc(1, sizeof(hardware_t))

void hardware_free(hardware_t *hw);

void hardware_load(lua_State *L, hardware_t *hw);

#endif /* HARDWARE_H_ */

