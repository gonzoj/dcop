#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "agent.h"
#include "algorithm.h"
#include "constraint.h"
#include "dcop.h"
#include "hardware.h"
#include "list.h"
#include "mgm.h"
#include "resource.h"

#include <sim_api.h>

static LIST_HEAD(algorithms);

void dcop_register_algorithm(algorithm_t *a) {
	list_add_tail(&a->_l, &algorithms);
}

static algorithm_t * dcop_get_algorithm(const char *name) {
	for_each_entry(algorithm_t, a, &algorithms) {
		if (!strcmp(a->name, name)) return a;
	}

	return NULL;
}

static void dcop_init_algorithms() {
	mgm_register();
}

agent_t * dcop_get_agent(dcop_t *dcop, int id) {
	for_each_entry(agent_t, a, &dcop->agents) {
		if (a->id == id) return a;
	}

	return NULL;
}

void dcop_refresh_hardware(dcop_t *dcop) {
	for_each_entry(resource_t, r, &dcop->hardware->view->resources) {
		resource_refresh(dcop->L, r);
	}
}

void dcop_refresh_agents(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_refresh(a);
	}
}

void dcop_refresh(dcop_t *dcop) {
	dcop_refresh_hardware(dcop);
	dcop_refresh_agents(dcop);
}

void dcop_merge_view(dcop_t *dcop) {
	view_clear(dcop->hardware->view);
	bool clear = true;

	for_each_entry(agent_t, a, &dcop->agents) {
		if (!clear && !view_compare(dcop->hardware->view, a->view)) {
			printf("warning: inconsistent hardware view\n");
		}

		view_merge(dcop->hardware->view, a->view, true);
		clear = false;
	}
}

static void dcop_free(dcop_t *dcop) {
	if (dcop) {
		if (dcop->L) {
			lua_close(dcop->L);
		}

		if (dcop->hardware) {
			hardware_free(dcop->hardware);
		}

		for_each_entry_safe(agent_t, a, _a, &dcop->agents) {
			list_del(&a->_l);
			agent_free(a);
		}

		free(dcop);
	}
}

static int __dcop_load(lua_State *L) {
	lua_getglobal(L, "__this");
	dcop_t *dcop = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!dcop) {
		printf("error: failed to retrieve '__this' userdata\n");

		return 0;
	}

	dcop->L = L;

	printf("loading hardware...\n");
	dcop->hardware = (hardware_t *) calloc(1, sizeof(hardware_t));
	lua_getfield(L, -1, "hardware");
	hardware_load(dcop->L, dcop->hardware);

	printf("loading agents...\n");
	dcop->number_of_agents = 0;
	lua_getfield(L, -1, "agents");
	int t = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, t)) {
		agent_t *a = agent_new();
		agent_load(dcop, a);
		printf("loading agent %i\n", a->id);

		list_add_tail(&a->_l, &dcop->agents);

		dcop->number_of_agents++;
	}

	lua_pushnil(L);
	for_each_entry(agent_t, a, &dcop->agents) {
		lua_next(L, t);

		printf("loading neighbors for agent %i\n", a->id);
		agent_load_neighbors(a);

		lua_pop(L, 1);
	}
	
	lua_pop(L, 1);

	return 0;
}

static int __dcop_load_agent(lua_State *L) {
	lua_getglobal(L, "__this");
	agent_t *agent = (agent_t *) lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!agent) {
		printf("error: failed to retrieve '__this' userdata\n");

		return 0;
	}

	agent->L = L;

	lua_getfield(L, -1, "agents");
	lua_pushnumber(L, agent->id);
	lua_gettable(L, -2);

	printf("loading view of agent %i\n", agent->id);
	agent_load_view(agent);

	agent_load_agent_view(agent);

	printf("loading constraints of agent %i...\n", agent->id);
	agent_load_constraints(agent);

	lua_setglobal(L, "__agent");

	lua_pop(L, 1);

	return 0;
}

static lua_State * dcop_create_lua_state(void *object, const char *file, int (*load)(lua_State *)) {
	lua_State *L = luaL_newstate();
	if (!L) {
		return NULL;
	}
	luaL_openlibs(L);

	lua_pushlightuserdata(L, object);
	lua_setglobal(L, "__this");

	lua_newtable(L);
	lua_setglobal(L, "__resources");

	lua_register(L, "__dcop_load", load);

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");
	char *path = strdup(lua_tostring(L, -1));
	path = (char *) realloc(path, strlen(path) + 1 + strlen(DCOP_ROOT_DIR) + strlen("/lua/?.lua") + 1);
	strcat(path, ";");
	strcat(path, DCOP_ROOT_DIR);
	strcat(path, "/lua/?.lua");
	lua_pushstring(L, path);
	lua_setfield(L, -3, "path");
	lua_pop(L, 2);
	free(path);

	if (luaL_dofile(L, file)) {
		printf("error: failed to load file '%s': %s\n", file, lua_tostring(L, -1));
		lua_pop(L, 1);

		lua_close(L);
		
		return NULL;
	}

	return L;
}

static struct dcop * dcop_load(const char *file) {
	dcop_t *dcop = (dcop_t *) calloc(1, sizeof(dcop_t));

	INIT_LIST_HEAD(&dcop->agents);

	if (!dcop_create_lua_state(dcop, file, __dcop_load)) {
		dcop->L = NULL;
		dcop_free(dcop);

		return NULL;
	}

	for_each_entry(agent_t, a, &dcop->agents) {
		if (!dcop_create_lua_state(a, file, __dcop_load_agent)) {
			a->L = NULL;

			dcop_free(dcop);

			return NULL;
		}
	}

	return dcop;
}

int dcop_get_number_of_cores() {
	return sysconf(_SC_NPROCESSORS_ONLN);
}

int main(int argc, char **argv) {
	dcop_init_algorithms();

	if (argc < 2) {
		printf("error: no dcop specification given\n");
		exit(1);
	}

	char *spec = argv[argc - 1];

	printf("number of cores available: %i\n", dcop_get_number_of_cores());

	printf("loading dcop specification from '%s'\n", spec);
	dcop_t *dcop = dcop_load(spec);
	if (!dcop) {
		printf("error: failed to load dcop specification\n");
		exit(1);
	}
	printf("completed loading '%s'\n", spec);

	if (dcop->number_of_agents > dcop_get_number_of_cores()) {
		printf("warning: number of agents exceeds available physical cores\n");
	}

	printf("\ninital resource assignment:\n");
	view_dump(dcop->hardware->view);
	printf("\n");

	algorithm_t *mgm = dcop_get_algorithm("mgm");
	printf("initialize algorithm '%s'\n", mgm->name);
	mgm->init(dcop, 0, NULL);

	SimRoiStart();

	printf("starting algorithm '%s'\n\n", mgm->name);
	mgm->run(dcop);

	SimRoiEnd();

	mgm->cleanup(dcop);
	printf("\nalgorithm '%s' finished\n", mgm->name);

	printf("\nprevious resource assignment:\n");
	view_dump(dcop->hardware->view);

	dcop_refresh(dcop);

	dcop_merge_view(dcop);

	printf("\nfinal resource assignment:\n");
	view_dump(dcop->hardware->view);

	dcop_free(dcop);

	free_native_constraints();

	exit(0);
}

