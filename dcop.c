#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>

#include "dcop.h"

static int native_constraints_n = 0;
static constraint **native_constraints = NULL;

void register_native_constraint(constraint *c) {
	native_constraints = (constraint **) realloc(native_constraints, ++native_constraints_n * sizeof(constraint *));
	native_constraints[native_constraints_n - 1] = c;
}

static double constraint_evaluate_lua(constraint *c) {
	lua_getglobal(c->L, "__constraints");
	lua_rawgeti(c->L, -1, c->ref);
	lua_getfield(c->L, -1, "eval");
	lua_getfield(c->L, -2, "args");
	if (lua_pcall(c->L, 1, 1, 0)) {
		printf("error: failed to call function 'eval' evaluating constraint (%s)\n", lua_tostring(c->L, -1));
		lua_pop(c->L, 3);

		return 0;
	}
	double rating = lua_tonumber(c->L, -1);

	lua_pop(c->L, 3);

	return rating;
}

static object_type check_object_type_lua(lua_State *L) {
	object_type result;

	lua_getmetatable(L, -1);
	lua_getfield(L, -1, "__object_type");
	const char *type = lua_tostring(L, -1);
	if (!strcmp(type, "resource")) {
		result = OBJECT_TYPE_RESOURCE;
	} else if (!strcmp(type, "hardware")) {
		result = OBJECT_TYPE_HARDWARE;
	} else if (!strcmp(type, "agent")) {
		result = OBJECT_TYPE_AGENT;
	} else if (!strcmp(type, "dcop")) {
		result = OBJECT_TYPE_DCOP;
	} else if (!strcmp(type, "constraint")) {
		result = OBJECT_TYPE_CONSTRAINT;
	} else {
		result = OBJECT_TYPE_UNKNOWN;
	}

	lua_pop(L, 2);

	return result;
}

static void constraint_load(lua_State *L, constraint *c) {
	lua_getfield(L, -1, "name");
	c->name = strdup(lua_tostring(L, -1));

	c->type = CONSTRAINT_TYPE_LUA;
	constraint *_c;
	for_each_entry(nativ_constraints, _c) {
		if (!strcmp(c->name, _c->name)) {
			c->type = CONSTRAINT_TYPE_NATIVE;
			c->eval = _c->eval;
		}
	}
	if (c->type == CONSTRAINT_TYPE_LUA) c->eval = constraint_evaluate_lua;

	lua_getfield(L, -2, "args"):
	c->args_n = lua_objlen(L, -1);
	c->args = (constraint_arg *) calloc(c->args_n, sizeof(constraint_arg));
	int t = lua_gettop(L);
	lua_pushnil(L);
	for (int i = 0; i < c->args_n && lua_next(L, t); i++) {
		switch (check_object_type_lua(L)) {
			case OBJECT_TYPE_AGENT:
				c->args[i].type = OBJECT_TYPE_AGENT;
				lua_getfield(L, -1, "id");
				int id = lua_tonumber(L, -1);
				lua_getglobal(L, "__dcop");
				dcop *dcop = lua_touserdata(L, -1);
				lua_pop(L, 2);
				if (!dcop) {
					printf("error: failed to retrieve '__dcop' userdata");

					lua_pop(L, 2);

					constraint_free(c);

					return;
				} else {
					c->args[i].agent = dcop_get_agent(dcop, id);
				}
				break;

			case OBJECT_TYPE_CONSTRAINT:
				c->args[i].type = OBJECT_TYPE_CONSTRAINT;
				constraint *arg = (constraint *) calloc(1, sizeof(constraint));
				constraint_load(L, arg);
				c->args[i].constraint = arg;
				break;

			default:
				printf("error: only obect type 'agent' and 'constraint' possible for constraint arguments");

				lua_pop(L, 2);

				constraint_free(c);

				return;
		}

		lua_pop(L, 1);
	}

	c->L = L;
	lua_getglobal(L, "__constraints");
	lua_pushvalue(L, -2);
	c->ref = luaL_ref(L, -2);
	lua_pop(L, 2);
}

static void constraint_free(constraint *c) {
	if (c) {
		if (c->name) free(c->name);

		if (c->args) {
			constraint_arg arg;
			for_each_entry(c->args, arg) {
				switch (arg.type) {
					case OBJECT_TYPE_CONSTRAINT:
						if (arg.constraint) {
							constraint_free(arg.constraint)
							free(arg.constraint);
						}
						break;

					default:
						break;
				}
			}
			free(c->args);
		}
	}
}

static void resource_free(resource *r) {
	if (r && r->type) free(r->type);
}

static void hardware_free(hardware *hw) {
	if (hw && hw->resources) {
		for_each(hw->resources, resource_free)
		free(hw->resources);
	}
}

static void agent_free(agent *a) {
	if (a) {
		if (a->view) {
			for_each(a->view, resource_free)
			free(a->view);
		}

		if (a->neighbors) free(a->neighbors);

		if (a->constraints) {
			for_each(a->constraints, constraint_free)
			free(a->constraints);
		}
	}
}

static void dcop_free(dcop *dcop) {
	if (dcop) {
		if (dcop->L) lua_close(dcop->L);

		if (dcop->hardware) {
			hardware_free(dcop->hardware);
			free(dcop->hardware);
		}

		if (dcop->agents) {
			for_each(dcop->agents, agent_free)
			free(dcop->agents);
		}

		free(dcop);
	}
}

static void resource_load(lua_State *L, resource *r) {
	lua_getfield(L, -1, "type");
	r->type = strdup(lua_tostring(L, -1));

	lua_getfield(L, -2, "status");
	r->status = lua_tonumber(L, -1);

	lua_getfield(L, -3, "tile");
	r->tile = lua_tonumber(L, -1);

	lua_pop(L, 4);
}

static void agent_load(lua_State *L, agent *a) {
	lua_getfield(L, -1, "id");
	a->id = lua_tonumber(L, -1);

	lua_getfield(L, -2, "view");
	int t = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, t)) {
		a->view = (resource *) realloc(a->view, ++i * sizeof(resource));
		resource_load(L, &a->view[i - 1]);
		lua_pop(L, 1);
	}
	
	a->neighbors_n = 0;

	lua_pop(L, 3);
}

static void hardware_load(lua_State *L, hardware *hw) {
	lua_getfield(L, -1, "number_of_tiles");
	hw->tiles_n = lua_tonumber(L, -1);

	lua_getfield(L, -1, "resources");
	int t = lua_gettop(L);
	hw->resources_n = 0;
	lua_pushnil(L);
	while (lua_next(L, t)) {
		hw->resources = (resource *) realloc(hw->resources, ++hw->resources_n * sizeof(resource));
		resource_load(L, &hw->resources[hw->resources_n - 1]);
	}

	lua_pop(L, 3);
}

agent * dcop_get_agent(dcop *dcop, int id) {
	for (int i = 0; i < dcop->agents_n; i++) {
		if (dcop->agents[i].id == id) return &dcop->agents[i];
	}

	return NULL;
}

static int __dcop_load(lua_State *L) {
	lua_getglobal(L, "__dcop");
	dcop *dcop = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!dcop) {
		printf("error: failed to retrieve '__dcop' userdata");

		return 0;
	}

	dcop->hardware = (hardware *) calloc(1, sizeof(hardware));
	lua_getfield(L, -1, "hardware");
	hardware_load(L, dcop->hardware);

	lua_getfield(L, -1, "agents");
	int t = lua_gettop(L);
	dcop->agents_n = 0;
	dcop->agents = NULL;
	lua_pushnil(L);
	while (lua_next(L, t)) {
		dcop->agents = (agent *) realloc(dcop->agents, ++dcop->agents_n * sizeof(agent));
		agent_load(L, &dcop->agents[dcop->agents_n - 1]);
	}

	t = lua_gettop(L);
	lua_pushnil(L);
	for (int i = 0; lua_next(L, t), i++) {
		lua_getfield(L, -1, "neighbors");
		lua_pushvalue(L, -2);
		if (lua_pcall(L, 1, 1, 0)) {
			printf("error: failed to call function 'neighbors' while loading agents (%s)\n", lua_tostring(L, -1));
			lua_pop(L, 3);

			return 0;
		}

		int _t = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, _t)) {
			dcop->agents[i].neighbors = (agent **) realloc(dcop->agents[i].neighbors, ++dcop->agents[i].neighbors_n * siezof(agent *));
			lua_getfield(L, -1, "id");
			int id = lua_tonumber(L, -1);
			dcop->agents[i].neighbors[dcop->agents[i].neighbors_n - 1] = dcop_get_agent(dcop, id);

			lua_pop(L, 2);
		}
		// pcall "neighbors(this)" result
		lua_pop(L, 1);

		lua_getfield(L, -1, "constraints");
		_t = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, _t)) {
			dcop->agents[i].constraints = (constraint *) realloc(dcop->agents[i].constraints, ++dcop->agents[i].constraints_n * sizeof(constraint));
			constraint_load(L, &dcop->agents[i].constraints[dcop->agents[i].constraints_n - 1]);
		}
		// field "constraints"
		lua_pop(L, 1);

		// agent[i]
		lua_pop(L, 1);
	}
	
	// field "agents"
	lua_pop(L, 1);

	return 0;
}

dcop * dcop_load(const char *file) {
	dcop *dcop = (dcop *) calloc(1, sizeof(dcop));
	dcop->L = NULL;

	dcop->L = luaL_newstate();
	if (!dcop->L) {
		goto error;
	}
	luaL_openlibs(dcop->L);

	lua_pushlightuserdata(dcop->L, dcop);
	lua_setglobal(dcop->L, "__dcop");

	lua_register(dcop->L, "__dcop_load", __dcop_load);

	if (luaL_dofile(dcop->L, file)) {
		printf("error: failed to load file '%s': %s\n", file, lua_tostring(p->L, -1));
		lua_pop(docp-L, 1);

		goto error;
	}

	return dcop;

error:
	dcop_free(dcop);

	return NULL;
}

