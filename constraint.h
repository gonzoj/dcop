#ifndef CONSTRAINT_H_
#define CONSTRAINT_H_

#include <lua.h>

#include "agent.h"
#include "dcop.h"
#include "hardware.h"
#include "list.h"
#include "resource.h"

typedef struct constraint {
	struct list_head _l;
	enum {
		CONSTRAINT_TYPE_NATIVE,
		CONSTRAINT_TYPE_LUA
	} type;
	char *name; struct list_head args;
	lua_State *L;
	int ref;
	double (*eval)(struct constraint *);
} constraint_t;

typedef enum {
	OBJECT_TYPE_AGENT,
	OBJECT_TYPE_CONSTRAINT,
	OBJECT_TYPE_DCOP,
	OBJECT_TYPE_HARDWARE,
	OBJECT_TYPE_RESOURCE,
	OBJECT_TYPE_UNKNOWN
} object_type_t;

typedef struct constraint_arg {
	struct list_head _l;
	object_type_t type;
	union {
		agent_t *agent;
		constraint_t *constraint;
		dcop_t *dcop;
		hardware_t *hardware;
		resource_t *resource;
		int ref;
	};
} constraint_arg_t;

constraint_t * constraint_new();

void constraint_free(constraint_t *c);

constraint_arg_t * constraint_arg_new(object_type_t type);

void constraint_arg_free(constraint_arg_t *arg);

void register_native_constraint(constraint_t *c);

void free_native_constraints();

void constraint_load(agent_t *agent, constraint_t *c);

#endif /* CONSTRAINT_H_ */

