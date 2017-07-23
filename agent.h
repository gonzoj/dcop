#ifndef AGENT_H_
#define AGENT_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include <lua.h>

#include "dcop.h"
#include "list.h"
#include "tlm.h"
#include "view.h"

typedef struct agent {
	struct list_head _l;
	dcop_t *dcop;
	lua_State *L;
	int id;
	pthread_t tid;
	pthread_mutex_t mt;
	pthread_cond_t cv;
	struct list_head msg_queue;
	view_t *view;
	view_t **agent_view;
	int number_of_neighbors;
	struct list_head neighbors;
	struct list_head constraints;
	bool has_native_constraints;
	bool has_lua_constraints;
	tlm_t *tlm;
} agent_t;

typedef struct neighbor {
	struct list_head _l;
	agent_t *agent;
	tlm_t *tlm;
} neighbor_t;

typedef struct message {
	struct list_head _l;
	agent_t *from;
	void *buf;
	void (*free)(tlm_t *, void *);
	tlm_t *tlm;
} message_t;

extern bool use_tlm;

agent_t * agent_new();

void agent_free(agent_t *a);

neighbor_t * neighbor_new(tlm_t *tlm, agent_t *a);

#define neighbor_free(n) tlm_free(n->tlm, n)

message_t * message_new(tlm_t *tlm, void *buf, void (*free)(tlm_t *, void *));

#define message_free(msg) do { msg->free(msg->tlm, msg->buf); tlm_free(msg->tlm, msg); } while (0)

void agent_load(dcop_t *dcop, agent_t *a);

void agent_load_view(agent_t *a);

void agent_load_neighbors(agent_t *a);

void agent_load_agent_view(agent_t *a);

void agent_load_constraints(agent_t *a);

void agent_clear_agent_view(agent_t *a);

double agent_evaluate(agent_t *a);

double agent_evaluate_view(agent_t *a, view_t *v);

void agent_send(agent_t *s, agent_t *r, message_t *msg);

message_t * agent_recv(agent_t *r);

message_t * agent_recv_filter(agent_t *r, bool (*filter)(message_t *, void *), void *arg);

void agent_refresh(agent_t *a);

int agent_create_thread(agent_t *a, void * (*algorithm)(void *), void *arg);

void * agent_cleanup_thread(agent_t *a);

#define agent_is_owner(a, r) (r->status == RESOURCE_STATUS_TAKEN && r->owner == a->id)

void agent_dump_view(agent_t *a);

#define agent_has_neighbors(a) (a->number_of_neighbors > 0)

#define agent_claim_resource(a, r) do { r->status = RESOURCE_STATUS_TAKEN; r->owner = a->id; } while (0)

#define agent_yield_resource(r) do { r->status = RESOURCE_STATUS_FREE; } while (0)

bool agent_has_conflicting_view(agent_t *a, int id);

#endif /* AGENT_H_ */

