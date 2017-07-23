#include <getopt.h>
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
	bool stale;
	bool changed;
	double initial_eval;
	double best_eval;
	int max_resources;
} mgm_agent_t;

typedef enum {
	MGM_WAIT_OK_MODE,
	MGM_WAIT_IMPROVE_MODE
} mgm_mode_t;

static algorithm_t _mgm;

static int max_distance = 200;

static bool consistent = true;

#define min(x, y) (x < y ? x : y)

#define mgm_message(m) ((mgm_message_t *) m->buf)

#define DEBUG_MESSAGE(a, f, v...) do { console_lock(); print_debug("[%i]: ", a->agent->id); DEBUG print(f, ## v); console_unlock(); } while (0)

static void mgm_message_free(tlm_t *tlm, void *buf) {
	mgm_message_t *msg = (mgm_message_t *) buf;

	if (msg) {
		if (msg->type == MGM_OK && msg->view) {
			view_free(msg->view);
		}

		tlm_free(tlm, msg);
	}
}

static message_t * mgm_message_new(mgm_agent_t *a, int type) {
	//mgm_message_t *msg = (mgm_message_t *) calloc(1, sizeof(mgm_message_t));
	//mgm_message_t *msg = (mgm_message_t *) dcop_malloc_aligned(sizeof(mgm_message_t));
	mgm_message_t *msg;
	if (a) {
		msg = (mgm_message_t *) tlm_malloc(a->agent->tlm, sizeof(mgm_message_t));
	} else {
		msg = (mgm_message_t *) calloc(1, sizeof(mgm_message_t));
	}
	
	msg->type = type;

	message_t *m;
	if (a) {
		m = message_new(a->agent->tlm, msg, mgm_message_free);
	} else {
		m = message_new(NULL, msg, mgm_message_free);
	}

	return m;
}

static int send_ok(mgm_agent_t *a) {
	// IMPROVEMENT: stop algorithm if no agent has changed its resource assignment
	if (++a->term == max_distance || a->stale) {
		if (a->stale) {
			DEBUG_MESSAGE(a, "stopping algorithm due to stale resource assignment\n");
		}

		for_each_entry(neighbor_t, n, &a->agent->neighbors) {
			agent_send(a->agent, n->agent, mgm_message_new(a, MGM_END));
		}

		return -1;
	}

	if (a->can_move) {
		char *s = view_to_string(a->new_view);
		DEBUG_MESSAGE(a, "updating to view (%f):\n%s", a->improve, s);
		free(s);

		view_copy(a->agent->view, a->new_view);
	}

	for_each_entry(neighbor_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(a, MGM_OK);
		mgm_message(msg)->view = view_clone(a->agent->view);
		agent_send(a->agent, n->agent, msg);
	}

	return 0;
}

static double get_improvement(mgm_agent_t *a, double *eval) {
	double _eval = agent_evaluate_view(a->agent, a->new_view);

	double improve;
	if (!isfinite(_eval) && (eval ? !isfinite(*eval) : !isfinite(a->eval))) {
		improve = 0;
	} else if (!isfinite(_eval)) {
		improve = -INFINITY;
	} else {
		improve = (eval ? *eval : a->eval) - _eval;
		if (eval && improve > 0) {
			*eval = _eval;
		}
	}

	return improve;
}

static bool permutate_assignment(mgm_agent_t *a, resource_t *r, int pos, view_t **new_view, double *new_eval) {
	// IMPROVEMENT: stop when optimal utility is acquired
	if (new_eval != &a->best_eval && *new_eval == a->best_eval) {
		return true;
	}

	// IMPROVEMENT: stop when at maximum number of acquired resources necessary for optimal utility
	// PROBLEM: if optimal utility can't be reached, next best utility might require more resources
	//if (pos == a->agent->dcop->hardware->number_of_resources || (a->max_resources >= 0 && view_count_resources(a->new_view, a->agent->id) >= a->max_resources)) {	
	if (pos == a->agent->dcop->hardware->number_of_resources) {
		/*
		console_lock();
		DEBUG_MESSAGE(a, "trying assignment with utility %f:\n", agent_evaluate_view(a->agent, a->new_view));
		view_dump(a->new_view);
		console_unlock();
		*/

		double improve = get_improvement(a, new_eval);
		if (improve > 0) {
			char *s = view_to_string(a->new_view);
			DEBUG_MESSAGE(a, "considering new view with improvement %f:\n%s", improve, s);
			free(s);

			view_copy(*new_view, a->new_view);

			a->improve += improve;

			if (new_eval != &a->best_eval && *new_eval == a->best_eval) {
				DEBUG_MESSAGE(a, "found assignment with optimal utility\n");
				return true;
			}

			if (*new_eval == 0) {
				DEBUG_MESSAGE(a, "utility 0 is optimal\n");
				return true;
			}
		} else if (improve == 0 && new_eval == &a->best_eval && view_count_resources(*new_view, a->agent->id) < view_count_resources(a->new_view, a->agent->id)) {
			view_copy(*new_view, a->new_view);
		}
		return false;
	} else {
		/*double improve = get_improvement(a, new_eval);
		if (improve > 0) {
			char *s = view_to_string(a->new_view);
			DEBUG_MESSAGE(a, "considering new view with improvement %f:\n%s", improve, s);
			free(s);

			view_copy(*new_view, a->new_view);

			a->improve += improve;

			if (new_eval != &a->best_eval && *new_eval == a->best_eval) {
				DEBUG_MESSAGE(a, "found assignment with optimal utility\n");
				return true;
			}
		}*/

		resource_t *next = list_entry(r->_l.next, resource_t, _l);
		pos++;

		if (agent_is_owner(a->agent, r)) {
			bool result = false;

			result |= permutate_assignment(a, next, pos, new_view, new_eval);
			if (result) {
				return result;
			}

			agent_yield_resource(r);
			result |= permutate_assignment(a, next, pos, new_view, new_eval);

			return result;
		} else {
			bool result = false;

			int status = r->status;
			int owner = r->owner;

			result |= permutate_assignment(a, next, pos, new_view, new_eval);
			if (result) {
				return result;
			}

			agent_claim_resource(a->agent, r);
			result |= permutate_assignment(a, next, pos, new_view, new_eval);

			r->status = status;
			r->owner = owner;

			return result;
		}
	}
}

static void find_assignment(mgm_agent_t *a) {
	DEBUG_MESSAGE(a, "trying to find new assignment\n");

	a->improve = 0;

	view_copy(a->new_view, a->agent->view);

	view_t *new_view = view_clone(a->new_view);
	double new_eval = a->eval;

	permutate_assignment(a, list_first_entry(&a->new_view->resources, resource_t, _l), 0, &new_view, &new_eval);
	view_copy(a->new_view, new_view);

	view_free(new_view);
}

static bool try_free_resources(mgm_agent_t *a) {
	a->improve = 0;

	view_copy(a->new_view, a->agent->view);

	view_t *_view = a->new_view;

	int pos = 0;

	view_t *free_list = view_clone(a->new_view);
	for_each_entry_safe(resource_t, r, _r, &free_list->resources) {
		if (!resource_is_free(r)) {
			list_del(&r->_l);
			resource_free(r);

			pos++;
		}
	}

	if (pos == a->agent->dcop->hardware->number_of_resources) {
		return false;
	}

	a->new_view = free_list;

	view_t *new_view = view_clone(a->new_view);
	double new_eval = a->eval;

	bool result = permutate_assignment(a, list_first_entry(&a->new_view->resources, resource_t, _l), pos, &new_view, &new_eval);

	a->new_view = _view;
	if (result) {
		view_update(a->new_view, new_view);
	}

	view_free(new_view);

	view_free(free_list);

	return result;
}

static void improve(mgm_agent_t *a) {
	a->eval = agent_evaluate(a->agent);

	// IMPROVEMENT: don't try to improve if resource assignment hasn't changed
	if (a->improve == 0 && !a->changed) {
		DEBUG_MESSAGE(a, "resource assignment unchanged\n");
		return;
	}

	a->improve = 0;

	// IMPROVEMENT: don't try to improve when already at optimal utility
	if (a->eval == a->best_eval) {
		DEBUG_MESSAGE(a, "already at optimal utility\n");
		return;
	}

	// IMPROVEMENT: don't try to improve immediately after having changed assignment
	if (a->can_move) {
		DEBUG_MESSAGE(a, "changed assignment last round\n");
		return;
	}

	DEBUG_MESSAGE(a, "tyring to improve... (utility: %f)\n", a->eval);

	// IMPROVEMENT: try to acquire optimal utility by only looking at free resources
	if (try_free_resources(a)) {
		DEBUG_MESSAGE(a, "free resources satisfied constraints\n");
	} else {
		find_assignment(a);
	}
}

static void send_improve(mgm_agent_t *a) {
	improve(a);

	if (a->improve > 0) {
		a->can_move = true;
	} else {
		a->can_move = false;
	}

	for_each_entry(neighbor_t, n, &a->agent->neighbors) {
		message_t *msg = mgm_message_new(a, MGM_IMPROVE);
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

	tlm_touch(a->agent->tlm);

	dcop_start_ROI(a->agent->dcop);

	a->term = 0;
	a->can_move = false;
	a->eval = 0;
	a->improve = 0;
	a->new_view = view_clone(a->agent->view);	

	a->consistent = true;

	a->stale = false;

	a->changed = true;

	a->initial_eval = agent_evaluate(a->agent);

	for_each_entry(resource_t, r, &a->new_view->resources) {
		r->status = RESOURCE_STATUS_FREE;
	}
	a->best_eval = agent_evaluate_view(a->agent, a->new_view);
	a->max_resources = -1;

	view_t *best_view = view_clone(a->new_view);

	permutate_assignment(a, list_first_entry(&a->new_view->resources, resource_t, _l), 0, &best_view, &a->best_eval);
	DEBUG_MESSAGE(a, "optimal utility: %f\n", a->best_eval);

	a->max_resources = view_count_resources(best_view, a->agent->id);
	DEBUG_MESSAGE(a, "with %i resources\n", a->max_resources);

	view_free(best_view);

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

				if (counter == a->agent->number_of_neighbors) {
					if (!a->can_move) {
						for_each_entry(neighbor_t, n, &a->agent->neighbors) {
							if (!view_compare(a->agent->view, a->agent->agent_view[n->agent->id])) {
								char *s = view_to_string(a->agent->agent_view[n->agent->id]);
								//DEBUG_MESSAGE(a, "replacing view with agent_view[%i]\n%s", n->agent->id, s);
								free(s);

								if (view_is_affected(a->agent->view, a->agent->id, a->agent->agent_view[n->agent->id])) {
									a->changed = true;
								}

								view_copy(a->agent->view, a->agent->agent_view[n->agent->id]);
								break;
							}
						}
					} else {
						a->changed = true;
					}

					send_improve(a);

					counter = 0;

					a->consistent = true;

					a->stale = true;

					mode = MGM_WAIT_IMPROVE_MODE;
				}
				break;

			case MGM_IMPROVE:
				counter++;

				a->term = min(a->term, mgm_message(msg)->term);

				if (mgm_message(msg)->improve > a->improve || (mgm_message(msg)->improve == a->improve && a->agent->id > msg->from->id)) {
					a->can_move = false;
				}

				if (mgm_message(msg)->eval == INFINITY) {
					DEBUG_MESSAGE(a, "agent %i reported eval %f\n", msg->from->id, mgm_message(msg)->eval);
					a->consistent = false;
				}

				if (mgm_message(msg)->improve > 0) {
					a->stale = false;
				}

				if (counter == a->agent->number_of_neighbors) {
					if (a->improve > 0) {
						a->stale = false;
					}

					if (send_ok(a)) {
						stop = true;
						break;
					}

					agent_clear_agent_view(a->agent);

					counter = 0;

					a->changed = false;

					mode = MGM_WAIT_OK_MODE;
				}
				break;

			case MGM_END:
				stop = true;
				break;

			case MGM_START:
				if (!agent_has_neighbors(a->agent)) {
					// algorithm not really suited for that case, not sure what to do here...
					improve(a);
					if (a->improve > 0) {
						view_copy(a->agent->view, a->new_view);
					}
					stop = true;
				} else if (send_ok(a)) {
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

static void mgm_usage() {
	printf("\n");
	printf("OPTIONS:\n");
	printf("	--distance MAX, -d MAX\n");
	printf("		maximum distance used by agents\n");
	printf("\n");
}

static int parse_arguments(int argc, char **argv) {
	struct option long_options[] = {
		{ "distance", required_argument, NULL, 'd' },
		{ 0 }
	};

	optind = 1;

	while (true) {
		int result = getopt_long(argc, argv, "d:", long_options, NULL);
		if (result == -1) {
			break;
		}

		int distance;

		switch (result) {
			case 'd':
				distance = (int) strtol(optarg, NULL, 10);
				if (distance <= 0) {
					print_error("mgm: invalid distance given\n");
					print("mgm: using default distance %i\n", max_distance);
				} else {
					max_distance = distance;
					print("mgm: distance set to %i\n", max_distance);
				}
				break;

			case '?':
			case ':':
			default:
				print_error("mgm: failed to parse algorithm paramters\n");
				return -1;
		}
	}

	return 0;
}

static void mgm_init(dcop_t *dcop, int argc, char **argv) {
	parse_arguments(argc, argv);

	for_each_entry(agent_t, a, &dcop->agents) {
		//mgm_agent_t *_a = (mgm_agent_t *) calloc(1, sizeof(mgm_agent_t));
		//mgm_agent_t *_a = (mgm_agent_t *) dcop_malloc_aligned(sizeof(mgm_agent_t));
		mgm_agent_t *_a = (mgm_agent_t *) tlm_malloc(a->tlm, sizeof(mgm_agent_t));

		_a->agent = a;

		//DEBUG_MESSAGE(_a, "initial view of agent:\n");
		//DEBUG agent_dump_view(_a->agent);

		agent_create_thread(a, mgm, _a);
	}
	DEBUG print("\n");

	consistent = true;
}

static void mgm_cleanup(dcop_t *dcop) {
	double total_initial_eval = 0;
	double total_optimal_eval = 0;
	double total_eval = 0;

	for_each_entry(agent_t, a, &dcop->agents) {
		mgm_agent_t *_a = (mgm_agent_t *) agent_cleanup_thread(a);

		agent_cleanup_thread(_a->agent);

		//DEBUG_MESSAGE(_a, "final view of agent:\n");
		//DEBUG agent_dump_view(_a->agent);

		DEBUG_MESSAGE(_a, "initial utility: %f\n", _a->initial_eval);
		DEBUG_MESSAGE(_a, "current utility: %f\n", _a->eval);

		view_free(_a->new_view);

		if (!_a->consistent) {
			consistent = false;
		}

		total_initial_eval += _a->initial_eval;
		total_optimal_eval += _a->best_eval;
		total_eval += _a->eval;

		tlm_free(_a->agent->tlm, _a);
	}
	DEBUG print("\n");

	DEBUG print("total initial utility: %f\n", total_initial_eval);
	DEBUG print("total optimal utility: %f\n", total_optimal_eval);
	DEBUG print("total current utility: %f\n", total_eval);
	DEBUG print("\n");

	if (!consistent) print_error("error: MGM algorithm finished in an incosistent state\n");
}

static void mgm_run(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_send(NULL, a, mgm_message_new(NULL, MGM_START));
	}
}

static void mgm_kill(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_send(NULL, a, mgm_message_new(NULL, MGM_END));
	}
}

void mgm_register() {
	_mgm = algorithm_new("mgm", mgm_init, mgm_cleanup, mgm_run, mgm_kill, mgm_usage);
	dcop_register_algorithm(&_mgm);
}

