#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "algorithm.h"
#include "console.h"
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
	bool consistent;
} mgm_agent_t;

typedef enum {
	MGM_WAIT_OK_MODE,
	MGM_WAIT_IMPROVE_MODE
} mgm_mode_t;

static int max_distance = 20;

static bool consistent = true;

#define min(x, y) (x < y ? x : y)

#define mgm_message(m) ((mgm_message_t *) m->buf)

#define DEBUG_MESSAGE(a, f, v...) do { print_debug("[%i]: ", a->agent->id); DEBUG print(f, ## v); } while (0)

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
	if (++a->term == max_distance) {
		for_each_entry(neighbor_t, n, &a->agent->neighbors) {
			agent_send(a->agent, n->agent, mgm_message_new(MGM_END));
		}

		return -1;
	}

	if (a->can_move) {
		char *s = view_to_string(a->new_view);
		DEBUG_MESSAGE(a, "updating to view:\n%s", s);
		free(s);

		view_copy(a->agent->view, a->new_view);
	}

	for_each_entry(neighbor_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(MGM_OK);
		mgm_message(msg)->view = view_clone(a->agent->view);
		agent_send(a->agent, n->agent, msg);
	}

	return 0;
}

static double get_improvement(mgm_agent_t *a) {
	view_t *_view = a->agent->view;

	a->agent->view = a->new_view;
	double eval = agent_evaluate(a->agent);

	a->agent->view = _view;

	double improve;
	if (!isfinite(eval) && !isfinite(a->eval)) {
		improve = 0;
	} else if (!isfinite(eval)) {
		improve = -INFINITY;
	} else {
		improve = a->eval - eval;
	}

	return improve;
}

static void permutate_assignment(mgm_agent_t *a, resource_t *r, int pos, view_t **new_view) {
	if (pos == a->agent->dcop->hardware->number_of_resources) {
		double improve = get_improvement(a);
		if (improve > a->improve) {
			char *s = view_to_string(a->agent->view);
			DEBUG_MESSAGE(a, "considering new view with improvement %f:\n%s", improve, s);
			free(s);

			view_copy(*new_view, a->new_view);

			a->improve = improve;
		}
	} else {
		resource_t *next = list_entry(r->_l.next, resource_t, _l);
		pos++;

		int status = r->status;
		int owner = r->owner;

		if (agent_is_owner(a->agent, r)) {
			r->status = RESOURCE_STATUS_FREE;
		}
		permutate_assignment(a, next, pos, new_view);

		r->status = RESOURCE_STATUS_TAKEN;
		r->owner = a->agent->id;
		permutate_assignment(a, next, pos, new_view);

		r->status = status;
		r->owner = owner;
	}
}

static void improve(mgm_agent_t *a) {
	a->eval = agent_evaluate(a->agent);
	a->improve = 0;

	view_copy(a->new_view, a->agent->view);

	view_t *new_view = view_clone(a->new_view);

	permutate_assignment(a, list_first_entry(&a->new_view->resources, resource_t, _l), 0, &new_view);

	if (a->improve > 0) {
		view_copy(a->new_view, new_view);
	}

	view_free(new_view);
}

static void send_improve(mgm_agent_t *a) {
	improve(a);

	if (a->improve > 0) {
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
}

static bool filter_mgm_message(message_t *msg, void *mode) {
	if (mgm_message(msg)->type == MGM_START || mgm_message(msg)->type == MGM_END) {
		return true;
	}

	if ((mgm_mode_t) mode == MGM_WAIT_OK_MODE) {
		return mgm_message(msg)->type == MGM_OK;
	} else if ((mgm_mode_t) mode == MGM_WAIT_IMPROVE_MODE) {
		return mgm_message(msg)->type == MGM_IMPROVE;
	} else {
		return true;
	}
}

static void * mgm(void *arg) {
	mgm_agent_t *a = (mgm_agent_t *) arg;

	a->term = 0;
	a->can_move = false;
	a->eval = 0;
	a->improve = 0;
	a->new_view = view_clone(a->agent->view);	

	a->consistent = true;

	mgm_mode_t mode = MGM_WAIT_OK_MODE;

	int counter = 0;

	bool stop = false;
	while (!stop) {
		//DEBUG_MESSAGE(a, "mgm: waiting for messages...\n");

		message_t *msg = agent_recv_filter(a->agent, filter_mgm_message, (void *) mode);

		/*const char *type_string;
		switch (mgm_message(msg)->type) {
			case MGM_OK: type_string = "MGM_OK"; break;
			case MGM_IMPROVE: type_string = "MGM_IMPROVE"; break;
			case MGM_END: type_string = "MGM_END"; break;
			case MGM_START: type_string = "MGM_START"; break;
		}
		DEBUG_MESSAGE(a, "received message (%s)\n", type_string);*/

		switch(mgm_message(msg)->type) {
			case MGM_OK:
				counter++;

				view_copy(a->agent->agent_view[msg->from->id], mgm_message(msg)->view);

				if (counter == a->agent->number_of_neighbors || !agent_has_neighbors(a->agent)) {
					if (!a->can_move) {
						for_each_entry(neighbor_t, n, &a->agent->neighbors) {
							if (!view_compare(a->agent->view, a->agent->agent_view[n->agent->id])) {
								char *s = view_to_string(a->agent->agent_view[n->agent->id]);
								DEBUG_MESSAGE(a, "replacing view with agent_view[%i]\n%s", n->agent->id, s);
								free(s);

								view_copy(a->agent->view, a->agent->agent_view[n->agent->id]);
								break;
							}
						}
					}

					send_improve(a);

					counter = 0;

					a->consistent = true;

					mode = MGM_WAIT_IMPROVE_MODE;
				}
				break;

			case MGM_IMPROVE:
				counter++;

				a->term = min(a->term, mgm_message(msg)->term);

				if (mgm_message(msg)->improve > a->improve || (mgm_message(msg)->improve == a->improve && a->agent->id > msg->from->id)) {
					a->can_move = false;
				}

				if (mgm_message(msg)->eval > 0) {
					a->consistent = false;
				}

				if (counter == a->agent->number_of_neighbors || !agent_has_neighbors(a->agent)) {
					if (send_ok(a)) {
						stop = true;
						break;
					}

					agent_clear_agent_view(a->agent);

					counter = 0;

					mode = MGM_WAIT_OK_MODE;
				}
				break;

			case MGM_END:
				stop = true;
				break;

			case MGM_START:
				if (send_ok(a)) {
					stop = true;
				}
				break;
		}

		message_free(msg);
	}

	// pthread_exit crashes sniper/valgrind with signal 4 illegal instruction?
	//pthread_exit(agent);
	return (void *) a;
}

static void mgm_init(dcop_t *dcop, int argc, char **argv) {
	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) calloc(1, sizeof(mgm_agent_t));

		_a->agent = a;

		DEBUG_MESSAGE(_a, "initial view of agent:\n");
		DEBUG agent_dump_view(_a->agent);

		agent_create_thread(a, mgm, _a);
	}

	consistent = true;
}

static void mgm_cleanup(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) agent_cleanup_thread(a);

		agent_cleanup_thread(_a->agent);

		DEBUG_MESSAGE(_a, "final view of agent:\n");
		DEBUG agent_dump_view(_a->agent);

		view_free(_a->new_view);

		if (!_a->consistent) {
			consistent = false;
		}

		free(_a);
	}

	if (!consistent) print_error("error: MGM algorithm finished in an incosistent state\n");
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

