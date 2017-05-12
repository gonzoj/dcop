#ifndef DCOP_H_
#define DCOP_H_

#include <pthread.h>

#include <lua.h>

#define for_each(l, o) for (int i = 0; i < l##_n; i++) { o(l[i]) }

#define for_each_entry(l, p) for (int i = 0; i < l##_n && (p = l[i]); i++) 

typedef struct {
	const char *type;
	int status;
	int tile;
} resource;

typedef struct {
	int resources_n;
	resource *resources;
	int tiles_n;
} hardware;

typedef struct {
	pthread_t tid;
	int id;
	resource *view;
	int neighbors_n;
	agent **neighbors;
	int constraints_n;
	constraint *constraints;
} agent;

typedef struct {
	lua_State *L;
	hardware *hardware;
	int agents_n;
	agent *agents;
} dcop;

#define arg (type == OBJECT_TYPE_AGENT ? agent : constraint)

typedef enum {
	OBJECT_TYPE_UNKNOWN,
	OBJECT_TYPE_RESOURCE,
	OBJECT_TYPE_HARDWARE,
	OBJECT_TYPE_AGENT,
	OBJECT_TYPE_DCOP,
	OBJECT_TYPE_CONSTRAINT
} object_type;

typedef struct {
	object_type type;
	union {
		agent *agent;
		constraint *constraint;
	};
} constraint_arg;

typedef struct {
	enum {
		CONSTRAINT_TYPE_NATIVE,
		CONSTRAINT_TYPE_LUA
	} type;
	const char *name;
	int args_n;
	constraint_arg *args;
	lua_State *L;
	int ref;
	double (*eval)(constraint *);
} constraint;

#endif /* DCOP_H_ */

