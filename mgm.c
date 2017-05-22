#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "algorithm.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"

typedef struct mgm_message {
	enum {
		MGM_OK,
		MGM_IMPROVE,
		MGM_END,
		MGM_START
	} type;
	union {
		struct list_head view;
		struct {
			double eval;
			double improve;
			int term;
		};
	};
} mgm_message_t;

typedef struct mgm_agent {
	int term;
	bool can_move;
	double eval;
	double improve;
	struct list_head new_view;
	agent_t *agent;
} mgm_agent_t;

static int max_distance = 200;

static bool consistent = true;

#define min(x, y) (x < y ? x : y)

#define mgm_message(m) ((mgm_message_t *) m->buf)

static message_t * mgm_message_new(int type) {
	mgm_message_t *msg = (mgm_message_t *) calloc(1, sizeof(mgm_message_t));
	
	msg->type = type;

	message_t *m = message_new(msg, free);

	return m;
}

static int send_ok(mgm_agent_t *a) {
	a->term++;
	if (a->term == max_distance) {
		for_each_entry(agent_t, n, &a->agent->neighbors) {
			agent_send(a->agent, n, mgm_message_new(MGM_END));
		}
		agent_send(a->agent, a->agent, mgm_message_new(MGM_END));

		return -1;
	}
	if (a->can_move) {
		agent_update_view(a->agent, &a->new_view);
	}
	for_each_entry(agent_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(MGM_OK);
		mgm_message(msg)->view = a->agent->view;
		agent_send(a->agent, n, msg);
	}
	message_t *msg = mgm_message_new(MGM_OK);
	mgm_message(msg)->view = a->agent->view;
	agent_send(a->agent, a->agent, msg);

	return 0;
}

static void permutate_assignment(mgm_agent_t *agent, struct list_head *l, int pos, double *new_eval, struct list_head *new_view) {
	if (pos == agent->agent->dcop->hardware->number_of_resources) {
		struct list_head _view = agent->agent->view;
		agent->agent->view = agent->new_view;
		agent_refresh(agent->agent->dcop, agent->agent);
		double eval = agent_evaluate(agent->agent);
		agent->agent->view = _view;
		agent_refresh(agent->agent->dcop, agent->agent);
		if ((isfinite(eval) && !isfinite(*new_eval)) || (isfinite(eval) && eval < *new_eval)) {
			*new_eval = eval;
			agent_clone_view(&agent->new_view, new_view);
		}
	} else {
		pos++;
		resource_t *r = list_entry(l, resource_t, _l);
		if (r->status == RESOURCE_STATUS_TAKEN && r->owner == agent->agent->id) {
			r->status = RESOURCE_STATUS_FREE;
		}
		permutate_assignment(agent, l->next, pos, new_eval, new_view);
		r->status = RESOURCE_STATUS_TAKEN;
		r->owner = agent->agent->id;
		permutate_assignment(agent, l->next, pos, new_eval, new_view);
	}
}

static void improve(mgm_agent_t *a) {
	double eval = INFINITY;
	LIST_HEAD(new_view);
	permutate_assignment(a, &a->new_view, 0, &eval, &new_view);
	a->improve = a->eval - eval;
	agent_free_view(&a->new_view);
	a->new_view = new_view;
}

static void send_improve(mgm_agent_t *a) {
	agent_refresh(a->agent->dcop, a->agent);
	a->eval = agent_evaluate(a->agent);
	improve(a);
	if (a->improve > 0) {
		a->can_move = true;
	} else {
		a->can_move = false;
	}
	for_each_entry(agent_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(MGM_IMPROVE);
		mgm_message(msg)->eval = a->eval;
		mgm_message(msg)->improve = a->improve;
		mgm_message(msg)->term = a->term;
		agent_send(a->agent, n, msg);
	}
	message_t *msg = mgm_message_new(MGM_IMPROVE);
	mgm_message(msg)->eval = a->eval;
	mgm_message(msg)->improve = a->improve;
	mgm_message(msg)->term = a->term;
	agent_send(a->agent, a->agent, msg);
}

static void * mgm(void *arg) {
	mgm_agent_t *agent = (mgm_agent_t *) arg;

	agent->term = 0;
	agent->can_move = false;
	agent->eval = 0;
	agent->improve = 0;
	INIT_LIST_HEAD(&agent->new_view);
	agent_clone_view(&agent->agent->view, &agent->new_view);	

	bool stop = false;
	int counter = 0;
	double improve = 0;
	while (!stop) {
		printf("mgm: waiting for messages...\n");
		message_t *msg = agent_recv(agent->agent);
		printf("received message (%i)\n", mgm_message(msg)->type);

		switch(mgm_message(msg)->type) {
			case MGM_OK:
				counter++;
				agent_merge_view(agent->agent, &mgm_message(msg)->view);
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					send_improve(agent);
					counter = 0;
				}
				break;

			case MGM_IMPROVE:
				counter++;
				agent->term = min(agent->term, mgm_message(msg)->term);
				if (mgm_message(msg)->improve > improve || (mgm_message(msg)->improve == improve && agent->agent->id > msg->from->id)) {
					agent->can_move = false;
				}
				if (mgm_message(msg)->eval > 0) consistent = false;
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					if (send_ok(agent)) {
						stop = true;
						break;
					}
					agent_clear_view(agent->agent);
					counter = 0;
				}
				break;

			case MGM_END:
				stop = true;
				break;

			case MGM_START:
				if (send_ok(agent)) {
					stop = true;
				}
				break;
		}

		message_free(msg);
	}

	pthread_exit(agent);
}

static void mgm_init(dcop_t *dcop, int argc, char **argv) {
	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) calloc(1, sizeof(mgm_agent_t));
		_a->agent = a;
		agent_create_thread(a, mgm, _a);
	}

	consistent = true;
}

static void mgm_cleanup(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) agent_cleanup_thread(a);
		agent_free_view(&_a->new_view);
		free(_a);
	}
}

static void mgm_run(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_send(NULL, a, mgm_message_new(MGM_START));
	}
}

static void mgm_kill(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_send(NULL, a, mgm_message_new(MGM_END));
	}
}

static algorithm_t _mgm = algorithm_new("mgm", mgm_init, mgm_cleanup, mgm_run, mgm_kill);

void mgm_register() {
	dcop_register_algorithm(&_mgm);
}

