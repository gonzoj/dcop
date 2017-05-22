#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "constraint.h"
#include "list.h"

static LIST_HEAD(native_constraints);

constraint_t * constraint_new() {
	constraint_t *c = (constraint_t *) calloc(1, sizeof(constraint_t));

	INIT_LIST_HEAD(&c->args);

	return c;
}

void constraint_free(constraint_t *c) {
	if (c) {
		if (c->name) free(c->name);

		for_each_entry_safe(constraint_arg_t, arg, _arg, &c->args) {
			list_del(&arg->_l);
			constraint_arg_free(arg);
		}

		free(c);
	}
}

constraint_arg_t * constraint_arg_new(object_type_t type) {
	constraint_arg_t *arg = (constraint_arg_t *) calloc(1, sizeof(constraint_arg_t));

	arg->type = type;

	return arg;
}

void constraint_arg_free(constraint_arg_t *arg) {
	if (arg) {
		switch (arg->type) {
			case OBJECT_TYPE_CONSTRAINT:
				constraint_free(arg->constraint);
				break;

			default:
				break;
		}

		free(arg);
	}
}

void register_native_constraint(constraint_t *c) {
	list_add_tail(&c->_l, &native_constraints);
}

void free_native_constraints() {
	for_each_entry(constraint_t, c, &native_constraints) {
		constraint_free(c);
	}
}

static double constraint_evaluate_lua(constraint_t *c) {
	lua_getglobal(c->L, "__constraints");
	lua_rawgeti(c->L, -1, c->ref);
	lua_getfield(c->L, -1, "eval");
	lua_getfield(c->L, -2, "args");
	if (lua_pcall(c->L, 1, 1, 0)) {
		printf("error: failed to call function 'eval' evaluating constraint '%s' (%s)\n", c->name, lua_tostring(c->L, -1));
		lua_pop(c->L, 3);

		return 0;
	}
	double rating = lua_tonumber(c->L, -1);

	lua_pop(c->L, 3);

	return rating;
}

static object_type_t check_object_type_lua(lua_State *L) {
	object_type_t result;

	if (!luaL_getmetafield(L, -1, "__object_type")) {
		return OBJECT_TYPE_UNKNOWN;
	}
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

	lua_pop(L, 1);

	return result;
}

void constraint_load(dcop_t *dcop, constraint_t *c) {
	lua_getfield(dcop->L, -1, "name");
	c->name = strdup(lua_tostring(dcop->L, -1));

	c->type = CONSTRAINT_TYPE_LUA;
	for_each_entry(constraint_t, _c, &native_constraints) {
		if (!strcmp(c->name, _c->name)) {
			c->type = CONSTRAINT_TYPE_NATIVE;
			c->eval = _c->eval;
		}
	}
	if (c->type == CONSTRAINT_TYPE_LUA) c->eval = constraint_evaluate_lua;

	lua_getfield(dcop->L, -2, "args");
	if (lua_type(dcop->L, -1) == LUA_TTABLE && check_object_type_lua(dcop->L) == OBJECT_TYPE_UNKNOWN) {
		int t = lua_gettop(dcop->L);
		lua_pushnil(dcop->L);
		while (lua_next(dcop->L, t)) {
			constraint_arg_t *arg = constraint_arg_new(check_object_type_lua(dcop->L));

			switch (arg->type) {
				case OBJECT_TYPE_AGENT:
					lua_getfield(dcop->L, -1, "id");
					int id = lua_tonumber(dcop->L, -1);
					arg->agent = dcop_get_agent(dcop, id);
					lua_pop(dcop->L, 2);
					break;

				case OBJECT_TYPE_CONSTRAINT:
					arg->constraint = constraint_new();
					constraint_load(dcop, arg->constraint);
					break;

				case OBJECT_TYPE_DCOP:
					arg->dcop = dcop;
					lua_pop(dcop->L, 1);
					break;

				case OBJECT_TYPE_HARDWARE:
					arg->hardware = dcop->hardware;
					lua_pop(dcop->L, 1);
					break;

				case OBJECT_TYPE_RESOURCE:
					arg->resource = resource_new();
					resource_load(dcop, arg->resource);
					break;

				default:
					printf("warning: object type of argument for constraint '%s' unknown\n", c->name);
					arg->ref = luaL_ref(dcop->L, LUA_GLOBALSINDEX);
					break;
			}

			list_add_tail(&arg->_l, &c->args);
		}
	} else {
		printf("error: constraint arguments must be within a table\n");
	}

	lua_pop(dcop->L, 2);

	c->L = dcop->L;
	lua_getglobal(dcop->L, "__constraints");
	lua_pushvalue(dcop->L, -2);
	c->ref = luaL_ref(dcop->L, -2);
	lua_pop(dcop->L, 2);
}

