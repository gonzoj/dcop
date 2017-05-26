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
#include "view.h"

typedef struct mgm_message {
	enum {
		MGM_OK,
		MGM_IMPROVE,
		MGM_END,
		MGM_START
	} type;
	union {
		view_t *view;
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
	view_t *new_view;
	agent_t *agent;
	dcop_t *dcop;
} mgm_agent_t;

static int max_distance = 10;

static bool consistent = true;

#define min(x, y) (x < y ? x : y)

#define mgm_message(m) ((mgm_message_t *) m->buf)

#define DEBUG(a, f, v...) printf("DEBUG: [%i]: ", a->agent->id); printf(f, ## v);

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
		printf("[%i] can move! updating to view ", a->agent->id);
		view_dump(a->new_view);
		agent_update_view(a->agent, view_clone(a->new_view));
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

static void permutate_assignment(mgm_agent_t *agent, struct list_head *l, int pos, double *new_eval, view_t **new_view) {
	if (pos == agent->dcop->hardware->number_of_resources) {
		agent_refresh(agent->dcop, agent->agent);
		double eval = agent_evaluate(agent->dcop, agent->agent);
		if ((isfinite(eval) && !isfinite(*new_eval)) || (isfinite(eval) && eval < *new_eval)) {
			printf("[%i] setting new view: ", agent->agent->id);
			view_dump(agent->agent->view);
			*new_eval = eval;
			view_free(*new_view);
			*new_view = view_clone(agent->new_view);
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
	view_t *_view = a->agent->view;
	a->agent->view = a->new_view;

	double eval = INFINITY;
	view_t *new_view = view_new();
	permutate_assignment(a, a->new_view->resources.next, 0, &eval, &new_view);
	DEBUG(a, "new eval: %f\n", eval);
	a->improve = a->eval - eval;
	DEBUG(a, "improve: %f\n", a->improve);
	if (a->improve > 0) {
		view_free(a->new_view);
		a->new_view = new_view;
	} else {
		view_free(new_view);
	}

	a->agent->view = _view;
}

static void send_improve(mgm_agent_t *a) {
	agent_refresh(a->dcop, a->agent);
	a->eval = agent_evaluate(a->dcop, a->agent);
	printf("[%i] eval: %f\n", a->agent->id, a->eval);
	improve(a);
	if (a->improve > 0) {
		DEBUG(a, "improve > 0 -> we can move!\n");
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
	agent->new_view = view_clone(agent->agent->view);	

	bool stop = false;
	int counter = 0;
	while (!stop) {
		//printf("mgm: waiting for messages...\n");
		message_t *msg = agent_recv(agent->agent);
		const char *type_string;
		switch (mgm_message(msg)->type) {
			case MGM_OK: type_string = "MGM_OK"; break;
			case MGM_IMPROVE: type_string = "MGM_IMPROVE"; break;
			case MGM_END: type_string = "MGM_END"; break;
			case MGM_START: type_string = "MGM_START"; break;
		}
		//printf("received message (%s)\n", type_string);

		switch(mgm_message(msg)->type) {
			case MGM_OK:
				counter++;
				view_merge(agent->agent->view, mgm_message(msg)->view);
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					send_improve(agent);
					counter = 0;
				}
				break;

			case MGM_IMPROVE:
				counter++;
				agent->term = min(agent->term, mgm_message(msg)->term);
				if (mgm_message(msg)->improve > agent->improve || (mgm_message(msg)->improve == agent->improve && agent->agent->id > msg->from->id)) {
					agent->can_move = false;
				}
				if (mgm_message(msg)->eval > 0) consistent = false;
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					if (send_ok(agent)) {
						DEBUG(agent, "are we breaking here?\n");
						stop = true;
						break;
					}
					printf("[%i] clearing view...\n", agent->agent->id);
					view_clear(agent->agent->view);
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
		_a->dcop = dcop;
		agent_create_thread(a, mgm, _a);
	}

	consistent = true;
}

static void mgm_cleanup(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) agent_cleanup_thread(a);
		printf("final view of agent %i:\n", _a->agent->id);
		view_dump(_a->agent->view);
		view_free(_a->new_view);
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

