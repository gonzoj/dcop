#include <stdlib.h>

#include <lua.h>

#include "dcop.h"
#include "hardware.h"
#include "list.h"
#include "resource.h"

void hardware_free(hardware_t *hw) {
	if (hw) {
		view_free(hw->view);

		free(hw);
	}
}

void hardware_load(dcop_t *dcop, hardware_t *hw) {
	lua_getfield(dcop->L, -1, "number_of_tiles");
	hw->number_of_tiles = lua_tonumber(dcop->L, -1);
	
	lua_getfield(dcop->L, -2, "resources");
	hw->view = view_new();
	hw->number_of_resources = view_load(dcop, hw->view);

	lua_pop(dcop->L, 3);
}

