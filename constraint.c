#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "agent.h"
#include "console.h"
#include "constraint.h"
#include "list.h"

static LIST_HEAD(native_constraints);

constraint_t * constraint_new() {
	constraint_t *c = (constraint_t *) calloc(1, sizeof(constraint_t));

	return c;
}

static void argument_free(argument_t *arg) {
	switch (arg->type) {
		case OBJECT_TYPE_CONSTRAINT:
			constraint_free(arg->constraint);
			break;

		case OBJECT_TYPE_STRING:
			free(arg->string);
			break;

		case OBJECT_TYPE_NUMBER:
		case OBJECT_TYPE_UNKNOWN:
		default:
			break;
	}
}

void constraint_free(constraint_t *c) {
	if (c) {
		if (c->name) {
			free(c->name);
		}

		if (c->param.neighbors) {
			free(c->param.neighbors);
		}
		if (c->param.args) {
			for (int i = 0; i < c->param.argc; i++) {
				argument_free(&c->param.args[i]);
			}

			free(c->param.args);
		}

		free(c);
	}
}

void register_native_constraint(const char *name, double (*eval)(struct constraint *)) {
	constraint_t *c = constraint_new();
	c->name = strdup(name);
	c->eval = eval;

	list_add_tail(&c->_l, &native_constraints);

	print("registered native constraint '%s'\n", name);
}

void free_native_constraints() {
	for_each_entry_safe(constraint_t, c, _c, &native_constraints) {
		list_del(&c->_l);
		constraint_free(c);
	}
}

double constraint_evaluate_lua(constraint_t *c) {
	lua_getglobal(c->L, "__constraints");
	lua_rawgeti(c->L, -1, c->ref);
	lua_getfield(c->L, -1, "eval");
	lua_getfield(c->L, -2, "param");
	if (lua_pcall(c->L, 1, 1, 0)) {
		print_error("failed to call function 'eval' evaluating constraint '%s' (%s)\n", c->name, lua_tostring(c->L, -1));
		lua_pop(c->L, 3);

		return 0;
	}
	double rating = lua_tonumber(c->L, -1);

	lua_pop(c->L, 3);

	return rating;
}

static object_type_t check_object_type_lua(lua_State *L) {
	if (lua_isnumber(L, -1)) {
		return OBJECT_TYPE_NUMBER;
	} else if (lua_isstring(L, -1)) {
		return OBJECT_TYPE_STRING;
	} else {
		if (!luaL_getmetafield(L, -1, "__object_type")) {
			return OBJECT_TYPE_UNKNOWN;
		}

		object_type_t result;

		const char *type = lua_tostring(L, -1);
		if (!strcmp(type, "constraint")) {
			result = OBJECT_TYPE_CONSTRAINT;
		} else {
			result = OBJECT_TYPE_UNKNOWN;
		}

		lua_pop(L, 1);

		return result;
	}
}

void constraint_load(agent_t *agent, constraint_t *c) {
	lua_getfield(agent->L, -1, "name");
	c->name = strdup(lua_tostring(agent->L, -1));

	c->type = CONSTRAINT_TYPE_LUA;
	for_each_entry(constraint_t, _c, &native_constraints) {
		if (!strcmp(c->name, _c->name)) {
			c->type = CONSTRAINT_TYPE_NATIVE;
			c->eval = _c->eval;
		}
	}
	if (c->type == CONSTRAINT_TYPE_LUA) {
		c->eval = constraint_evaluate_lua;

		agent->has_lua_constraints = true;
	} else {
		agent->has_native_constraints = true;
	}


	lua_getfield(agent->L, -2, "param");

	lua_getfield(agent->L, -1, "agent");
	lua_getfield(agent->L, -1, "id");
	int id = lua_tonumber(agent->L, -1);
	c->param.agent = dcop_get_agent(agent->dcop, id);
	lua_pop(agent->L, 2);

	c->param.neighbors = NULL;
	c->param.number_of_neighbors = 0;
	lua_getfield(agent->L, -1, "neighbors");
	int i = 0;
	int t = lua_gettop(agent->L);
	lua_pushnil(agent->L);
	while (lua_next(agent->L, t)) {
		c->param.neighbors = (int *) realloc(c->param.neighbors, ++c->param.number_of_neighbors * sizeof(int));
		c->param.neighbors[i++] = lua_tonumber(agent->L, -1);

		lua_pop(agent->L, 1);
	}
	lua_pop(agent->L, 1);

	c->param.args = NULL;
	c->param.argc = 0;
	lua_getfield(agent->L, -1, "args");
	i = 0;
	t = lua_gettop(agent->L);
	lua_pushnil(agent->L);
	while (lua_next(agent->L, t)) {
		c->param.args = (argument_t *) realloc(c->param.args, ++c->param.argc * sizeof(argument_t));

		c->param.args[i].type =  check_object_type_lua(agent->L);
		switch (c->param.args[i].type) {
			case OBJECT_TYPE_NUMBER:
				c->param.args[i].number = lua_tonumber(agent->L, -1);

				lua_pop(agent->L, 1);
				break;

			case OBJECT_TYPE_CONSTRAINT:
				c->param.args[i].constraint = constraint_new();
				constraint_load(agent, c->param.args[i].constraint);

				break;

			case OBJECT_TYPE_STRING:
				c->param.args[i].string = strdup(lua_tostring(agent->L, -1));

				lua_pop(agent->L, 1);
				break;

			case OBJECT_TYPE_UNKNOWN:
			default:
				print_warning("unknown object type of argument %i for constraint %s\n", i + 1, c->name);

				lua_pop(agent->L, 1);
				break;
		}

		i++;
	}

	lua_pop(agent->L, 3);

	c->L = agent->L;
	lua_getglobal(agent->L, "__constraints");
	lua_pushvalue(agent->L, -2);
	c->ref = luaL_ref(agent->L, -2);
	lua_pop(agent->L, 2);
}

