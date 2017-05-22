#ifndef DCOP_H_
#define DCOP_H_

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
};

void dcop_register_algorithm(algorithm_t *a);

struct agent * dcop_get_agent(dcop_t *dcop, int id);

void dcop_refresh_hardware(dcop_t *dcop);
void dcop_refresh_agents(dcop_t *dcop);
void dcop_refresh(dcop_t *dcop);

#endif /* DCOP_H_ */

