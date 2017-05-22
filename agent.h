#ifndef AGENT_H_
#define AGENT_H_

#include <pthread.h>
#include <stdlib.h>

#include "dcop.h"
#include "list.h"

typedef struct agent {
	struct list_head _l;
	dcop_t *dcop;
	int id;
	pthread_t tid;
	pthread_mutex_t mt;
	pthread_cond_t cv;
	struct list_head msg_queue;
	struct list_head view;
	int number_of_neighbors;
	struct list_head neighbors;
	struct list_head constraints;
} agent_t;

typedef struct neighbor {
	struct list_head _l;
	agent_t *agent;
} neighbor_t;

typedef struct message {
	struct list_head _l;
	agent_t *from;
	void *buf;
	void (*free)(void *);
} message_t;

agent_t * agent_new();

void agent_free(agent_t *a);

neighbor_t * neighbor_new(agent_t *a);

#define neighbor_free(n) free(n)

message_t * message_new(void *buf, void (*free)(void *));

#define message_free(msg) msg->free(msg->buf); free(msg)

void agent_load(dcop_t *dcop, agent_t *a);

void agent_load_neighbors(dcop_t *dcop, agent_t *a);

void agent_load_constraints(dcop_t *dcop, agent_t *a);

void agent_merge_view(agent_t *a, struct list_head *view);

void agent_clear_view(agent_t *a);

void agent_update_view(agent_t *agent, struct list_head *new);

void agent_clone_view(struct list_head *view, struct list_head *clone);

void agent_free_view(struct list_head *view);

double agent_evaluate(agent_t *a);

void agent_send(agent_t *s, agent_t *r, message_t *msg);

message_t * agent_recv(agent_t *r);

void agent_refresh(dcop_t *dcop, agent_t *a);

int agent_create_thread(agent_t *a, void * (*algorithm)(void *), void *arg);

void * agent_cleanup_thread(agent_t *a);

#endif /* AGENT_H_ */

