#include <stdlib.h>

#include <lua.h>

#include "hardware.h"
#include "view.h"

void hardware_free(hardware_t *hw) {
	if (hw) {
		view_free(hw->view);

		free(hw);
	}
}

void hardware_load(lua_State *L, hardware_t *hw) {
	lua_getfield(L, -1, "number_of_tiles");
	hw->number_of_tiles = lua_tonumber(L, -1);
	
	lua_getfield(L, -2, "resources");
	hw->view = view_new();
	hw->number_of_resources = view_load(L, hw->view);

	lua_pop(L, 3);
}

