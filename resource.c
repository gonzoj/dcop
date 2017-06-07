#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "resource.h"

void resource_free(resource_t *r) {
	if (r) {
		if (r->type) {
			free(r->type);
		}

		free(r);
	}
}

void resource_load(lua_State *L, resource_t *r) {
	lua_getfield(L, -1, "type");
	r->type = strdup(lua_tostring(L, -1));

	lua_getfield(L, -2, "status");
	r->status = lua_tonumber(L, -1);

	lua_getfield(L, -3, "owner");
	r->owner = lua_tonumber(L, -1);

	lua_getfield(L, -4, "tile");
	r->tile = lua_tonumber(L, -1);

	lua_pop(L, 4);

	lua_getglobal(L, "__resources");
	lua_pushvalue(L, -2);
	r->ref = luaL_ref(L, -2);

	lua_pop(L, 2);
}

void resource_refresh(lua_State *L, resource_t *r) {
	lua_getglobal(L, "__resources");
	lua_rawgeti(L, -1, r->ref);

	lua_pushnumber(L, r->status);
	lua_setfield(L, -2, "status");

	lua_pushnumber(L, r->owner);
	lua_setfield(L, -2, "owner");

	lua_pop(L, 2);
}

