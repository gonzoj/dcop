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

typedef enum {
	MGM_WAIT_OK_MODE,
	MGM_WAIT_IMPROVE_MODE
} mgm_mode_t;

static int max_distance = 5;

static bool consistent = true;

#define min(x, y) (x < y ? x : y)

#define mgm_message(m) ((mgm_message_t *) m->buf)

#define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
pthread_mutex_t print_m = PTHREAD_MUTEX_INITIALIZER;
#define DEBUG(a, f, v...) pthread_mutex_lock(&print_m); printf("DEBUG: [%i]: ", a->agent->id); printf(f, ## v); pthread_mutex_unlock(&print_m)
#else
#define DEBUG(a, f, v...)
#endif

static void mgm_message_free(void *buf) {
	mgm_message_t *msg = (mgm_message_t *) buf;

	if (msg) {
		if (msg->type == MGM_OK && msg->view) {
			view_free(msg->view);
		}

		free(msg);
	}
}

static message_t * mgm_message_new(int type) {
	mgm_message_t *msg = (mgm_message_t *) calloc(1, sizeof(mgm_message_t));
	
	msg->type = type;

	message_t *m = message_new(msg, mgm_message_free);

	return m;
}

static int send_ok(mgm_agent_t *a) {
	a->term++;
	if (a->term == max_distance) {
		for_each_entry(neighbor_t, n, &a->agent->neighbors) {
			agent_send(a->agent, n->agent, mgm_message_new(MGM_END));
		}
		//agent_send(a->agent, a->agent, mgm_message_new(MGM_END));

		return -1;
	}
	if (a->can_move) {
		char *s = view_to_string(a->new_view);
		DEBUG(a, "can move! updating to view:\n%s", s);
		free(s);
		pthread_mutex_lock(&a->dcop->mt);
		agent_update_view(a->agent, view_clone(a->new_view));
		pthread_mutex_unlock(&a->dcop->mt);
	}
	for_each_entry(neighbor_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(MGM_OK);
		mgm_message(msg)->view = view_clone(a->agent->view);
		agent_send(a->agent, n->agent, msg);
	}
	//message_t *msg = mgm_message_new(MGM_OK);
	//mgm_message(msg)->view = view_clone(a->agent->view);
	//agent_send(a->agent, a->agent, msg);

	return 0;
}

static void permutate_assignment(mgm_agent_t *agent, struct list_head *l, int pos, double *new_eval, view_t **new_view) {
	if (pos == agent->dcop->hardware->number_of_resources) {
		//view_dump(agent->agent->view);
		double eval = agent_evaluate(agent->dcop, agent->agent);
		if (isfinite(eval) && (!isfinite(*new_eval) || eval < *new_eval)) {
			char *s = view_to_string(agent->agent->view);
			DEBUG(agent, "setting new view with eval %f:\n%s", eval, s);
			free(s);
			*new_eval = eval;
			view_free(*new_view);
			*new_view = view_clone(agent->new_view);
		}
	} else {
		pos++;
		resource_t *r = list_entry(l, resource_t, _l);
		int status = r->status;
		int owner = r->owner;
		if (agent_is_owner(agent->agent, r)) {
			r->status = RESOURCE_STATUS_FREE;
			//printf("giving up resource %i\n", pos);
		} else if (r->status == RESOURCE_STATUS_TAKEN) {
			//printf("let %i have resource %i\n", r->owner, pos);
		} else {
			//printf("leave resource %i free\n", pos);
		}
		permutate_assignment(agent, l->next, pos, new_eval, new_view);
		//printf("claim resource %i\n", pos);
		r->status = RESOURCE_STATUS_TAKEN;
		r->owner = agent->agent->id;
		permutate_assignment(agent, l->next, pos, new_eval, new_view);
		r->status = status;
		r->owner = owner;
	}
}

static void improve(mgm_agent_t *a) {
	a->eval = agent_evaluate(a->dcop, a->agent);

	view_t *_view = a->agent->view;

	pthread_mutex_lock(&a->dcop->mt);
	view_free(a->new_view);
	a->new_view = view_clone(a->agent->view);
	a->agent->view = a->new_view;

	double eval = a->eval;
	DEBUG(a, "eval: %f\n", a->eval);
	//DEBUG(a, "for view:\n");
	//agent_dump_view(a->agent);
	pthread_mutex_unlock(&a->dcop->mt);
	view_t *new_view = view_new();

	pthread_mutex_lock(&a->dcop->mt);
	permutate_assignment(a, a->new_view->resources.next, 0, &eval, &new_view);

	a->agent->view = _view;
	pthread_mutex_unlock(&a->dcop->mt);

	DEBUG(a, "new eval: %f\n", eval);
	if (!isfinite(eval) && !isfinite(a->eval)) {
		a->improve = 0;
	} else if (!isfinite(eval)) {
		a->improve = -INFINITY;
	} else {
		a->improve = a->eval - eval;
	}
	DEBUG(a, "improve: %f\n", a->improve);
	if (a->improve > 0) {
		view_free(a->new_view);
		a->new_view = new_view;
	} else {
		view_free(new_view);
	}
}

static void send_improve(mgm_agent_t *a) {
	improve(a);
	if (a->improve > 0) {
		DEBUG(a, "improve > 0 -> we can move!\n");
		a->can_move = true;
	} else {
		a->can_move = false;
	}
	for_each_entry(neighbor_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(MGM_IMPROVE);
		mgm_message(msg)->eval = a->eval;
		mgm_message(msg)->improve = a->improve;
		mgm_message(msg)->term = a->term;
		agent_send(a->agent, n->agent, msg);
	}
	//message_t *msg = mgm_message_new(MGM_IMPROVE);
	//mgm_message(msg)->eval = a->eval;
	//mgm_message(msg)->improve = a->improve;
	//mgm_message(msg)->term = a->term;
	//agent_send(a->agent, a->agent, msg);
}

static bool filter_mgm_message(message_t *msg, void *mode) {
	if (mgm_message(msg)->type == MGM_START || mgm_message(msg)->type == MGM_END) return true;

	if ((mgm_mode_t) mode == MGM_WAIT_OK_MODE) {
		return mgm_message(msg)->type == MGM_OK;
	} else if ((mgm_mode_t) mode == MGM_WAIT_IMPROVE_MODE) {
		return mgm_message(msg)->type == MGM_IMPROVE;
	} else {
		return true;
	}
}

static void * mgm(void *arg) {
	mgm_agent_t *agent = (mgm_agent_t *) arg;

	agent->term = 0;
	agent->can_move = false;
	agent->eval = 0;
	agent->improve = 0;
	agent->new_view = view_clone(agent->agent->view);	

	mgm_mode_t mode = MGM_WAIT_OK_MODE;

	bool stop = false;
	int counter = 0;
	while (!stop) {
		//DEBUG(a, "mgm: waiting for messages...\n");

		message_t *msg = agent_recv_filter(agent->agent, filter_mgm_message, (void *) mode);

		/*const char *type_string;
		switch (mgm_message(msg)->type) {
			case MGM_OK: type_string = "MGM_OK"; break;
			case MGM_IMPROVE: type_string = "MGM_IMPROVE"; break;
			case MGM_END: type_string = "MGM_END"; break;
			case MGM_START: type_string = "MGM_START"; break;
		}
		DEBUG(a, "received message (%s)\n", type_string);*/

		switch(mgm_message(msg)->type) {
			case MGM_OK:
				counter++;
				view_copy(agent->agent->agent_view[msg->from->id], mgm_message(msg)->view);
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					if (!agent->can_move) {
						for_each_entry(neighbor_t, n, &agent->agent->neighbors) {
							if (!view_compare(agent->agent->view, agent->agent->agent_view[n->agent->id])) {
								DEBUG(agent, "merging view with agent_view[%i]\n", n->agent->id);
								view_copy(agent->agent->view, agent->agent->agent_view[n->agent->id]);
								break;
							}
						}
					}
					send_improve(agent);
					counter = 0;
					consistent = true;
					mode = MGM_WAIT_IMPROVE_MODE;
				}
				break;

			case MGM_IMPROVE:
				counter++;
				agent->term = min(agent->term, mgm_message(msg)->term);
				if (mgm_message(msg)->improve > agent->improve || (mgm_message(msg)->improve == agent->improve && agent->agent->id > msg->from->id)) {
					agent->can_move = false;
				}
				if (mgm_message(msg)->eval > 0) {
					consistent = false;
				}
				if (counter == agent->agent->number_of_neighbors || !agent->agent->number_of_neighbors) {
					if (send_ok(agent)) {
						stop = true;
						break;
					}
					agent_clear_view(agent->agent);
					counter = 0;
					mode = MGM_WAIT_OK_MODE;
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
		DEBUG(_a, "final view of agent:\n");
		//agent_dump_view(_a->agent);
		view_dump(_a->agent->view);
		view_free(_a->new_view);
		free(_a);
	}

	if (!consistent) printf("error: MGM algorithm finished in an incosistent state\n");

#ifdef DEBUG_OUTPUT
	pthread_mutex_destroy(&print_m);
#endif
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

