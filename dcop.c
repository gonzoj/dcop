#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
		resource_refresh(dcop, r);
	}
}

void dcop_refresh_agents(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_refresh(dcop, a);
	}
}

void dcop_refresh(dcop_t *dcop) {
	dcop_refresh_hardware(dcop);
	dcop_refresh_agents(dcop);
}

void dcop_merge_view(dcop_t *dcop) {
	view_clear(dcop->hardware->view);

	for_each_entry(agent_t, a, &dcop->agents) {
		view_merge(dcop->hardware->view, a->view, true);
	}
}

static void dcop_free(dcop_t *dcop) {
	if (dcop) {
		if (dcop->L) lua_close(dcop->L);

		if (dcop->hardware) hardware_free(dcop->hardware);

		for_each_entry_safe(agent_t, a, _a, &dcop->agents) {
			list_del(&a->_l);
			agent_free(a);
		}

		pthread_mutex_destroy(&dcop->mt);

		free(dcop);
	}
}

static int __dcop_load(lua_State *L) {
	lua_getglobal(L, "__dcop");
	dcop_t *dcop = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!dcop) {
		printf("error: failed to retrieve '__dcop' userdata\n");

		return 0;
	}

	printf("loading hardware...\n");
	dcop->hardware = (hardware_t *) calloc(1, sizeof(hardware_t));
	lua_getfield(L, -1, "hardware");
	hardware_load(dcop, dcop->hardware);

	printf("loading agents...\n");
	dcop->number_of_agents = 0;
	lua_getfield(L, -1, "agents");
	int t = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, t)) {
		agent_t *a = agent_new();
		agent_load(dcop, a);
		list_add_tail(&a->_l, &dcop->agents);
		dcop->number_of_agents++;
	}

	lua_pushnil(L);
	for_each_entry(agent_t, a, &dcop->agents) {
		lua_next(L, t);

		printf("loading neighbors for agent %i\n", a->id);
		agent_load_neighbors(dcop, a);

		printf("loading constraints for agent %i\n", a->id);
		agent_load_constraints(dcop, a);

		lua_pop(L, 1);
	}
	
	lua_pop(L, 1);

	return 0;
}

static struct dcop * dcop_load(const char *file) {
	dcop_t *dcop = (dcop_t *) calloc(1, sizeof(dcop_t));

	INIT_LIST_HEAD(&dcop->agents);

	dcop->L = luaL_newstate();
	if (!dcop->L) {
		goto error;
	}
	luaL_openlibs(dcop->L);

	lua_pushlightuserdata(dcop->L, dcop);
	lua_setglobal(dcop->L, "__dcop");

	lua_newtable(dcop->L);
	lua_setglobal(dcop->L, "__constraints");

	lua_newtable(dcop->L);
	lua_setglobal(dcop->L, "__resources");

	lua_newtable(dcop->L);
	lua_setglobal(dcop->L, "__agents");

	lua_register(dcop->L, "__dcop_load", __dcop_load);

	lua_getglobal(dcop->L, "package");
	lua_getfield(dcop->L, -1, "path");
	char *path = strdup(lua_tostring(dcop->L, -1));
	path = (char *) realloc(path, strlen(path) + 1 + strlen(DCOP_ROOT_DIR) + strlen("/lua/?.lua") + 1);
	strcat(path, ";");
	strcat(path, DCOP_ROOT_DIR);
	strcat(path, "/lua/?.lua");
	lua_pushstring(dcop->L, path);
	lua_setfield(dcop->L, -3, "path");
	lua_pop(dcop->L, 2);
	free(path);

	if (luaL_dofile(dcop->L, file)) {
		printf("error: failed to load file '%s': %s\n", file, lua_tostring(dcop->L, -1));
		lua_pop(dcop->L, 1);

		goto error;
	}

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&dcop->mt, &attr);
	pthread_mutexattr_destroy(&attr);

	return dcop;

error:
	dcop_free(dcop);

	return NULL;
}

int main(int argc, char **argv) {
	dcop_init_algorithms();

	if (argc < 2) {
		printf("error: no dcop specification given\n");
		exit(1);
	}

	char *spec = argv[argc - 1];

	printf("loading dcop specification from '%s'\n", spec);
	dcop_t *dcop = dcop_load(spec);
	if (!dcop) {
		printf("error: failed to load dcop specification\n");
		exit(1);
	}
	printf("completed loading '%s'\n", spec);

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

