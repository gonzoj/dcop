#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "dcop.h"
#include "resource.h"

void resource_free(resource_t *r) {
	if (r) {
		if (r->type) free(r->type);

		free(r);
	}
}

void resource_load(dcop_t *dcop, resource_t *r) {
	lua_getfield(dcop->L, -1, "type");
	r->type = strdup(lua_tostring(dcop->L, -1));

	lua_getfield(dcop->L, -2, "status");
	r->status = lua_tonumber(dcop->L, -1);

	lua_getfield(dcop->L, -3, "owner");
	r->owner = lua_tonumber(dcop->L, -1);

	lua_getfield(dcop->L, -4, "tile");
	r->tile = lua_tonumber(dcop->L, -1);

	lua_pop(dcop->L, 4);

	lua_getglobal(dcop->L, "__resources");
	lua_pushvalue(dcop->L, -2);
	r->ref = luaL_ref(dcop->L, -2);
	lua_pop(dcop->L, 2);
}

void resource_refresh(dcop_t *dcop, resource_t *r) {
	lua_getglobal(dcop->L, "__resources");
	lua_rawgeti(dcop->L, -1, r->ref);

	lua_pushnumber(dcop->L, r->status);
	lua_setfield(dcop->L, -2, "status");

	lua_pushnumber(dcop->L, r->owner);
	lua_setfield(dcop->L, -2, "owner");

	lua_pop(dcop->L, 2);
}

