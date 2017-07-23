#ifndef DCOP_H_
#define DCOP_H_

#include <pthread.h>
#include <stdbool.h>

#include <lua.h>

typedef struct dcop dcop_t;

#include "agent.h"
#include "algorithm.h"
#include "hardware.h"
#include "list.h"

struct dcop {
	lua_State *L;
	hardware_t *hardware;
	int number_of_agents;
	struct list_head agents;
	pthread_mutex_t m;
	int ready;
};

extern bool skip_lua;

void dcop_register_algorithm(algorithm_t *a);

struct agent * dcop_get_agent(dcop_t *dcop, int id);

void dcop_refresh_hardware(dcop_t *dcop);

void dcop_refresh_agents(dcop_t *dcop);

void dcop_refresh(dcop_t *dcop);

void dcop_merge_view(dcop_t *dcop);

int dcop_get_number_of_cores();

void * dcop_malloc_aligned(size_t size);

void dcop_start_ROI(dcop_t *dcop);

#endif /* DCOP_H_ */

