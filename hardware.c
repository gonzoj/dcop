#include <stdlib.h>

#include <lua.h>

#include "dcop.h"
#include "hardware.h"
#include "list.h"
#include "resource.h"

hardware_t * hardware_new() {
	hardware_t *hw = (hardware_t *) calloc(1, sizeof(hardware_t));

	INIT_LIST_HEAD(&hw->resources);

	return hw;
}

void hardware_free(hardware_t *hw) {
	if (hw) {
		for_each_entry_safe(resource_t, r, _r, &hw->resources) {
			list_del(&r->_l);
			resource_free(r);
		}

		free(hw);
	}
}

void hardware_load(dcop_t *dcop, hardware_t *hw) {
	lua_getfield(dcop->L, -1, "number_of_tiles");
	hw->number_of_tiles = lua_tonumber(dcop->L, -1);
	
	hw->number_of_resources = 0;
	INIT_LIST_HEAD(&hw->resources);
	lua_getfield(dcop->L, -2, "resources");
	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		resource_t *r = resource_new();
		resource_load(dcop, r);
		list_add_tail(&r->_l, &hw->resources);
		hw->number_of_resources++;
	}

	lua_pop(dcop->L, 3);
}

