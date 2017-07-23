#ifndef CONSTRAINT_H_
#define CONSTRAINT_H_

#include <lua.h>

#include "agent.h"
#include "list.h"
#include "tlm.h"

typedef enum {
	OBJECT_TYPE_NUMBER,
	OBJECT_TYPE_CONSTRAINT,
	OBJECT_TYPE_STRING,
	OBJECT_TYPE_UNKNOWN
} object_type_t;

typedef struct argument {
	object_type_t type;
	union {
		double number;
		struct constraint *constraint;
		char *string;
	};
	tlm_t *tlm;
} argument_t;

typedef struct parameter {
	agent_t *agent;
	int number_of_neighbors;
	int *neighbors;
	int argc;
	argument_t *args;
} parameter_t;

typedef struct constraint {
	struct list_head _l;
	enum {
		CONSTRAINT_TYPE_NATIVE,
		CONSTRAINT_TYPE_LUA
	} type;
	char *name;
	parameter_t param;
	lua_State *L;
	int ref;
	double (*eval)(struct constraint *);
	tlm_t *tlm;
} constraint_t;

constraint_t * constraint_new(tlm_t *tlm);

void constraint_free(constraint_t *c);

void register_native_constraint(const char *name, double (*eval)(struct constraint *));

void free_native_constraints();

double constraint_evaluate_lua(constraint_t *c);

void constraint_load(agent_t *agent, constraint_t *c);

#endif /* CONSTRAINT_H_ */

