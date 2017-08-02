#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lua.h>

#include "agent.h"
#include "algorithm.h"
#include "cluster.h"
#include "console.h"
#include "dcop.h"
#include "distrm.h"
#include "list.h"
#include "native.h"
#include "region.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

static algorithm_t _distrm;

static int cluster_size = 2;
static int try_limit = 5;
static int max_par_reqs = 2;
static int region_size = 2;
static int region_num = 3;
static int max_dist = 2;
static int locality_thresh = 1;
static int size_thresh = 5;
static int max_rounds = 3;
static int rounds = 0;
static int ready = 0;

static distrm_agent_t *idle_agent = NULL;

static distrm_agent_t *invading_agent = NULL;

static pthread_mutex_t distrm_m;
static pthread_cond_t distrm_cv;

void distrm_message_free(tlm_t *tlm, void *buf) {
	distrm_message_t *msg = (distrm_message_t *) buf;

	if (msg) {
		switch (msg->type) {
			case DISTRM_FORWARD:
				if (msg->neighbors) {
					tlm_free(tlm, msg->neighbors);
				}
				break;

			case DISTRM_OFFER:
				if (msg->offer) {
					view_free(msg->offer);
				}
				break;

			case DISTRM_ACCEPT:
			case DISTRM_REJECT:
				tlm_free(tlm, msg->core);
				break;

			default:
				break;
		}

		tlm_free(tlm, msg);
	}
}

message_t * distrm_message_new(tlm_t *tlm, int type) {
	distrm_message_t *msg = (distrm_message_t *) tlm_malloc(tlm, sizeof(distrm_message_t));

	msg->type = type;

	message_t *m = message_new(tlm, msg, sizeof(distrm_message_t), distrm_message_free);

	return m;
}

static bool distrm_invade(distrm_agent_t *a) {
	bool invading = false;

	pthread_mutex_lock(&distrm_m);

	if (!invading_agent) {
		invading_agent = a;

		invading = true;

		print("%i is invading!\n", a->agent->id);
	}
	
	if (++ready == a->agent->dcop->number_of_agents) {
		print("all clear\n");
		pthread_cond_broadcast(&distrm_cv);

		rounds++;
	} else {
		pthread_cond_wait(&distrm_cv, &distrm_m);
	}

	pthread_mutex_unlock(&distrm_m);

	return invading;
}

#define distrm_is_invading(a) (a == invading_agent)

bool distrm_is_idle_agent(int id) {
	return (idle_agent->agent->id == id);
}

resource_t * distrm_get_random_core(distrm_agent_t *a) {
	int index;
	random_r(&a->buf, &index);
	index %= a->agent->dcop->hardware->number_of_resources;

	return resource_clone(view_get_resource(a->agent->view, index));
}

static double random_d(distrm_agent_t *a) {
	int i;
	random_r(&a->buf, &i);

	return (double) i / RAND_MAX;
}

static double speedup(distrm_agent_t *a, view_t *v) {
	return _downey(a->A, a->sigma, v->size);
}

static double speedup_with_core(distrm_agent_t *a, view_t *v, resource_t *r) {
	resource_t *_r = resource_clone(r);
	view_add_resource(v, _r);

	double s = speedup(a, v);

	view_del_resource(v, _r);
	resource_free(_r);

	return s;
}

static double speedup_without_core(distrm_agent_t *a, view_t *v, resource_t *r) {
	view_t *_v = view_clone(v);

	resource_t *_r = view_get_resource(_v, r->index);
	view_del_resource(_v, _r);

	double s = speedup(a, _v);

	view_free(_v);

	return s;
}

static view_t * create_offer(distrm_agent_t *a, distrm_agent_t *c, region_t *region) {
	print("agent %i creating offer for agent %i\n", a->agent->id, c->agent->id);

	view_t *offered_cores = view_new_tlm(a->agent->tlm);

	if (distrm_is_idle_agent(a->agent->id)) {
		view_concat(offered_cores, a->owned_cores);

		view_cut(a->owned_cores, offered_cores);
		view_concat(a->reserved_cores, offered_cores);

		return offered_cores;
	}

	view_t *potential_cores = view_clone(a->owned_cores);
	for_each_entry_safe(resource_t, r, _r, &potential_cores->resources) {
		if (!region_contains(region, r)) {
			view_del_resource(potential_cores, r);
		}
	}
	double share_giver = (double) potential_cores->size / region->view->size;

	view_t *cores_receiver = view_clone(c->owned_cores);
	view_t *cores_giver = view_clone(a->owned_cores);

	double gain_total = INFINITY;
	while (gain_total > 0) {
		gain_total = 0;

		view_t *greedy_choice = view_new_tlm(a->agent->tlm);

		double base_receiver = speedup(c, cores_receiver);
		double base_giver = speedup(a, cores_giver);

		for_each_entry_safe(resource_t, r, __r, &potential_cores->resources) {
			double gain_receiver = share_giver * speedup_with_core(c, cores_receiver, r) - base_receiver;
			double loss_giver = base_giver - speedup_without_core(a, cores_giver, r);

			if (gain_receiver - loss_giver > gain_total) {
				view_add_resource(greedy_choice, resource_clone(r));

				gain_total = gain_receiver - loss_giver;
			}
		}

		if (gain_total > 0) {
			view_concat(offered_cores, greedy_choice);

			view_cut(potential_cores, offered_cores);

			view_concat(cores_receiver, offered_cores);
			view_cut(cores_giver, offered_cores);
		}

		tlm_free(a->agent->tlm, greedy_choice);
	}

	view_free(cores_giver);
	view_free(cores_receiver);

	view_free(potential_cores);

	view_cut(a->owned_cores, offered_cores);
	view_concat(a->reserved_cores, offered_cores);

	return offered_cores;
}

agent_t * distrm_get_agent(dcop_t *dcop, int id) {
	return (!distrm_is_idle_agent(id) ? dcop_get_agent(dcop, id) : idle_agent->agent);
}

static void send_request(distrm_agent_t *a, region_t *region) {
	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
	distrm_message(msg)->region = region;
	distrm_message(msg)->from = a;

	agent_t *handler = cluster_resolve_core(a, region->center);

	agent_send(a->agent, handler, msg);

	print("%i sent REQUEST to %i\n", a->agent->id, handler->id);
}

static bool filter_distrm_offer(message_t *msg, void *unused) {
	if (distrm_message(msg)->type == DISTRM_END) {
		return true;
	}

	if (distrm_message(msg)->type == DISTRM_MAKE_OFFER) {
		return true;
	}

	return (distrm_message(msg)->type == DISTRM_OFFER);
}

/*
static bool filter_distrm_response(message_t *msg, void *unused) {
	if (distrm_message(msg)->type == DISTRM_END) {
		return true;
	}

	return (distrm_message(msg)->type == DISTRM_ACCEPT || distrm_message(msg)->type == DISTRM_REJECT);
}
*/

static void handle_make_offer(distrm_agent_t *a, message_t *msg) {
	message_t *offer = distrm_message_new(a->agent->tlm, DISTRM_OFFER);

	distrm_message(offer)->offer = view_new_tlm(a->agent->tlm);

	view_concat(distrm_message(offer)->offer, create_offer(a, distrm_message(msg)->from, distrm_message(msg)->region));

	int size = distrm_message(offer)->offer->size;

	print("agent %i: created offer with %i cores\n", a->agent->id, size);

	agent_send(a->agent, msg->from, offer);

	print("agent %i: sent offer to managing agent %i\n", a->agent->id, msg->from->id);

	/*
	for (int i = 0; i < size; i++) {
		message_t *response = agent_recv_filter(a->agent, filter_distrm_response, NULL);

		if (distrm_message(response)->type == DISTRM_REJECT) {
			print("received reject message\n");
		} else if (distrm_message(response)->type == DISTRM_ACCEPT) {
			print("received accept message\n");
		} else {
			print("received something weird %i\n", distrm_message(response)->type);
		}

		resource_t *r = view_get_resource(a->reserved_cores, distrm_message(response)->index);
		view_del_resource(a->reserved_cores, r);
		if (distrm_message(response)->type == DISTRM_REJECT) {
			view_add_resource(a->owned_cores, r);
			print("agent %i: core %i got rejected by agent %i\n", a->agent->id, distrm_message(response)->core->index, response->from->id);
		} else {
			print("agent %i: core %i got accepted by agent %i\n", a->agent->id, distrm_message(response)->core->index, response->from->id);
		}

		message_free(response);
	}
	*/
}

// TODO: this shit deadlocks if two agents receive REQUEST/FORWARD and are neighbors of each other...
static void handle_request(distrm_agent_t *a, message_t *msg) {
	message_t *response = distrm_message_new(a->agent->tlm, DISTRM_OFFER);

	distrm_message(response)->offer = view_new_tlm(a->agent->tlm);

	for (int i = 0; i < distrm_message(msg)->num_neighbors; i++) {
		message_t *forward = distrm_message_new(a->agent->tlm, DISTRM_MAKE_OFFER);
		distrm_message(forward)->region = distrm_message(msg)->region;
		distrm_message(forward)->from = distrm_message(msg)->from;

		agent_send(a->agent, distrm_message(msg)->neighbors[i], forward);	
	}

	for (int i = 0; i < distrm_message(msg)->num_neighbors; i++) {
		message_t *remote_offer =  agent_recv_filter(a->agent, filter_distrm_offer, NULL);

		if (distrm_message(remote_offer)->type != DISTRM_OFFER) {
			handle_make_offer(a, remote_offer);
		} else {
			view_concat(distrm_message(response)->offer, distrm_message(remote_offer)->offer);

			message_free(remote_offer);
		}
	}

	if (a != distrm_message(msg)->from) {
		view_concat(distrm_message(response)->offer, create_offer(a, distrm_message(msg)->from, distrm_message(msg)->region));
	}

	print("sending a response to %i (from %i)\n", distrm_message(msg)->from->agent->id, a->agent->id);
	agent_send(a->agent, distrm_message(msg)->from->agent, response);
}

static void handle_request_message(distrm_agent_t *a, message_t *msg) {
	int *cores = tlm_malloc(a->agent->tlm, (a->agent->dcop->number_of_agents + 2) * sizeof(int));

	for_each_entry(resource_t, r, &distrm_message(msg)->region->view->resources) {
		agent_t *owner = cluster_resolve_core(a, r);
		cores[owner->id]++;
	}

	message_t *forward = distrm_message_new(a->agent->tlm, DISTRM_FORWARD);
	distrm_message(forward)->region = distrm_message(msg)->region;
	distrm_message(forward)->from = distrm_message(msg)->from;

	agent_t *handler = NULL;

	distrm_message(forward)->num_neighbors = 0;
	distrm_message(forward)->neighbors = NULL;

	int max = 0;
	for (int i = 1; i < a->agent->dcop->number_of_agents + 2; i++) {
		if (cores[i] > max) {
			max = cores[i];
			handler = distrm_get_agent(a->agent->dcop, i);
		}
	}

	for (int i = 1; i < a->agent->dcop->number_of_agents + 2; i++) {
		//if (cores[i] > 0 && i != a->agent->id && (!handler || a == distrm_message(msg)->from || i != handler->id)) {
		if (cores[i] > 0 && i != distrm_message(msg)->from->agent->id && (!handler || i != handler->id)) {
			distrm_message(forward)->num_neighbors++;
			distrm_message(forward)->neighbors = tlm_realloc(a->agent->tlm, distrm_message(forward)->neighbors, distrm_message(forward)->num_neighbors * sizeof(agent_t *));
			distrm_message(forward)->neighbors[distrm_message(forward)->num_neighbors - 1] = distrm_get_agent(a->agent->dcop, i);
		}
	}

	tlm_free(a->agent->tlm, cores);

	if (handler != a->agent && a != distrm_message(msg)->from && handler != distrm_message(msg)->from->agent) {
		print("%i: forwarding message to handler %i\n", a->agent->id, handler->id);
		agent_send(a->agent, handler, forward);
	} else {
		handle_request(a, forward);

		message_free(forward);
	}
}

#define min(x, y) (x < y ? x : y)

// TODO: deadlock if agent that is using request_cores (and waits for n offers is owning most cores in the selected region (and gets FORWARD message or REQUEST if region is far)
static view_t * request_cores(distrm_agent_t *a) {
	if (!a->core) {
		a->core = distrm_get_random_core(a);
		print("DistRM Agent %i seeded core: %i\n", a->agent->id, a->core->index);
	}

	view_t *offers = view_new_tlm(a->agent->tlm);

	for (int tries = 0; tries < try_limit && offers->size == 0; tries++) {
		struct list_head *potential_regions = region_select_random(a, region_size, region_num);

		int i = min(region_num, a->agent->dcop->hardware->number_of_resources);
		for_each_entry_safe(region_t, region, _r, potential_regions) {
			if (region_distance(region, a) < tries * (max_dist / try_limit) && random_d(a) > (double) 1 / tries) {
				list_del(&region->_l);
				i--;
			}
		}

		while (i > max_par_reqs) {
			region_t *region = region_get_most_distant(a, potential_regions);
			list_del(&region->_l);
			i--;
		}

		struct list_head *subregions = NULL;

		int n = 0;

		for_each_entry(region_t, region, potential_regions) {
			print("trying region around core %i\n", region->center->index);

			if (region_distance(region, a) > locality_thresh) {
				if (cluster_resolve_core(a, region->center)->id != a->agent->id) {
					send_request(a, region);
				} else {
					message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
					distrm_message(msg)->region = region;
					distrm_message(msg)->from = a;

					handle_request_message(a, msg);
				}

				n++;
			} else {
				if (region->size > size_thresh) {
					subregions = region_split(region, size_thresh);

					for_each_entry_safe(region_t, subregion, _s, subregions) {
						if (cluster_resolve_core(a, subregion->center)->id != a->agent->id) {
							send_request(a, subregion);
						} else {
							message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
							distrm_message(msg)->region = subregion;
							distrm_message(msg)->from = a;

							handle_request_message(a, msg);
						}

						n++;
					}
				} else {
					print("agent %i: handling request\n", a->agent->id);

					message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
					distrm_message(msg)->region = region;
					distrm_message(msg)->from = a;

					handle_request_message(a, msg);

					n++;
				}
			}
		}

		for (int i = 0; i < n; i++) {
			message_t *offer = agent_recv_filter(a->agent, filter_distrm_offer, NULL);

			print("(%i of %i) %i: received offer from agent %i\n", i + 1, n, a->agent->id, offer->from->id);

			view_concat(offers, distrm_message(offer)->offer);

			message_free(offer);
		}

		region_free_all(a, subregions);

		region_free_all(a, potential_regions);

	}

	return offers;
}

static void handle_offer(distrm_agent_t *a, view_t *offer) {
	double base = speedup(a, a->owned_cores);
	for_each_entry(resource_t, r, &offer->resources) {
		double s = speedup_with_core(a, a->owned_cores, r);

		agent_t *owner = distrm_get_agent(a->agent->dcop, r->owner);

		if (s > base) {
			print("accepting core %i\n", r->index);

			message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_ACCEPT);
			//distrm_message(msg)->core = resource_clone(r);
			distrm_message(msg)->core = resource_new_tlm(a->agent->tlm);
			distrm_message(msg)->core->index = r->index;
			distrm_message(msg)->index = r->index;

			print("sending accept for core %i\n", distrm_message(msg)->core->index);
			agent_send(a->agent, owner, msg);

			cluster_register_core(a, r);

			base = s;

			view_add_resource(a->owned_cores, resource_clone(r));
		} else {
			print("rejecting core %i\n", r->index);

			message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REJECT);
			//distrm_message(msg)->core = resource_clone(r);
			distrm_message(msg)->core = resource_new_tlm(a->agent->tlm);
			distrm_message(msg)->core->index = r->index;
			distrm_message(msg)->index = r->index;

			print("sending reject for core %i\n", distrm_message(msg)->core->index);
			agent_send(a->agent, owner, msg);
		}
	}

	view_free(offer);
}

static void distrm_kill(dcop_t *);

static void * distrm(void *arg) {
	distrm_agent_t *a = (distrm_agent_t *) arg;

	a->rounds = 0;

	if (!distrm_is_idle_agent(a->agent->id)) {
		dcop_start_ROI(a->agent->dcop);
	}

	bool stop = false;
	while (!stop) {
		print("%i: waiting for messages...\n", a->agent->id);

		message_t *msg = agent_recv(a->agent);

		const char *type_string;
		switch (distrm_message(msg)->type) {
			case DISTRM_REQUEST: type_string = "DISTRM_REQUEST"; break;
			case DISTRM_FORWARD: type_string = "DISTRM_FORWARD"; break;
			case DISTRM_MAKE_OFFER: type_string = "DISTRM_MAKE_OFFER"; break;
			case DISTRM_ACCEPT: type_string = "DISTRM_ACCEPT"; break;
			case DISTRM_REJECT: type_string = "DISTRM_REJECT"; break;
			case DISTRM_START: type_string = "DISTRM_START"; break;
			case DISTRM_END: type_string = "DISTRM_END"; break;
			default: type_string = "DISTRM_SOMETHING"; break;
		}

		print("%i: received %s (%i)\n", a->agent->id, type_string, distrm_message(msg)->type);

		resource_t *r;

		switch(distrm_message(msg)->type) {
			case DISTRM_REQUEST:
				handle_request_message(a, msg);
				break;

			case DISTRM_FORWARD:
				handle_request(a, msg);
				break;

			case DISTRM_MAKE_OFFER:
				handle_make_offer(a, msg);
				break;

			case DISTRM_ACCEPT:
				print("received accept message\n");
				r = view_get_resource(a->reserved_cores, distrm_message(msg)->index);
				view_del_resource(a->reserved_cores, r);
				print("agent %i: core %i got accepted by agent %i\n", a->agent->id, distrm_message(msg)->core->index, msg->from->id);

				break;

			case DISTRM_REJECT:
				print("received reject message\n");
				r = view_get_resource(a->reserved_cores, distrm_message(msg)->index);
				view_del_resource(a->reserved_cores, r);
				view_add_resource(a->owned_cores, r);
				print("agent %i: core %i got rejected by agent %i\n", a->agent->id, distrm_message(msg)->core->index, msg->from->id);
				break;

			case DISTRM_START:
				//if (a->owned_cores->size == 0 && !distrm_is_idle_agent(a->agent->id)) {
				if (rounds < max_rounds && !distrm_is_idle_agent(a->agent->id)) {
					print("trying to invade (%i)\n", a->agent->id);
					if (distrm_invade(a)) {
						print("oh yeah\n");
						view_t *offer = request_cores(a);
						handle_offer(a, offer);

						a->rounds++;

						invading_agent = NULL;

						ready = 0;

						print("\n\nNEXT ROUND BABY (%i)\n\n", a->agent->id);

						if (rounds < max_rounds) {
							agent_broadcast(a->agent, a->agent->dcop, distrm_message_new(a->agent->tlm, DISTRM_START));
							//agent_send(a->agent, idle_agent->agent, distrm_message_new(a->agent->tlm, DISTRM_START));
						} else {
							agent_broadcast(a->agent, a->agent->dcop, distrm_message_new(a->agent->tlm, DISTRM_END));
							agent_send(a->agent, idle_agent->agent, distrm_message_new(a->agent->tlm, DISTRM_END));

							cluster_stop();

							stop = true;
						}
						//distrm_kill(a->agent->dcop);
					}
					print("did we? (%i)\n", a->agent->id);
				}
				break;

			case DISTRM_END:
				stop = true;
				break;

			default:
				print("DistRM agent received unsupport message type %i\n", distrm_message(msg)->type);
				break;
		}

		message_free(msg);
	}

	if (!distrm_is_idle_agent(a->agent->id)) {
		dcop_stop_ROI(a->agent->dcop);
	}

	return (void *) a;
}

static void distrm_usage() {
	printf("\n");
	printf("OPTIONS:\n");
	printf("	--cluster SIZE, -c SIZE\n");
	printf("		size of core clusters on NoC\n");
	printf("\n");
}

static int parse_arguments(int argc, char **argv) {
	struct option long_options[] = {
		{ "cluster", required_argument, NULL, 'c' },
		{ 0 }
	};

	optind = 1;

	while (true) {
		int result = getopt_long(argc, argv, "c:", long_options, NULL);
		if (result == -1) {
			break;
		}

		int size;

		switch (result) {
			case 'c':
				size = (int) strtol(optarg, NULL, 10);
				if (size <= 0) {
					print_error("distrm: invalid cluster size given\n");
					print("distrm: using default cluster size %i\n", cluster_size);
				} else {
					cluster_size = size;
					print("distrm: cluster size set to %i\n", size);
				}
				break;

			case '?':
			case ':':
			default:
				print_error("distrm: failed to parse algorithm paramters\n");
				return -1;
		}
	}

	return 0;
}

static double * distrm_retrieve_downey_params(dcop_t *dcop, int id) {
	lua_getglobal(dcop->L, "downey_params");
	if (lua_isnil(dcop->L, -1)) {
		lua_pop(dcop->L, 1);

		return NULL;
	}

	lua_pushnumber(dcop->L, id);
	lua_gettable(dcop->L, -2);

	double *param = malloc(2 * sizeof(double));

	lua_getfield(dcop->L, -1, "A");
	param[0] = lua_tonumber(dcop->L, -1);
	lua_pop(dcop->L, 1);

	lua_getfield(dcop->L, -1, "sigma");
	param[1] = lua_tonumber(dcop->L, -1);
	lua_pop(dcop->L, 1);

	lua_pop(dcop->L, 2);

	return param;
}

static void distrm_init(dcop_t *dcop, int argc, char **argv) {
	parse_arguments(argc, argv);

	pthread_mutex_init(&distrm_m, NULL);
	pthread_cond_init(&distrm_cv, NULL);

	cluster_load(dcop, cluster_size);

	agent_t *agent = agent_new();
	agent->id = dcop->number_of_agents + 1;

	agent->dcop = dcop;

	idle_agent = (distrm_agent_t *) tlm_malloc(agent->tlm, sizeof(distrm_agent_t));
	idle_agent->agent = agent;

	agent->view = view_new_tlm(agent->tlm);

	idle_agent->owned_cores = view_new_tlm(agent->tlm);
	idle_agent->reserved_cores = view_new_tlm(agent->tlm);

	for_each_entry(resource_t, r, &dcop->hardware->view->resources) {
		resource_t *_r = resource_new_tlm(agent->tlm);
		memcpy(_r, r, sizeof(resource_t));
		_r->tlm = agent->tlm;
		_r->type = tlm_strdup(_r->tlm, r->type);

		view_add_resource(agent->view, _r);

		if (resource_is_free(_r)) {
			_r = resource_clone(_r);
			view_add_resource(idle_agent->owned_cores, _r);

			cluster_register_core(idle_agent, _r);
		}
	}

	idle_agent->stale = true;

	agent_create_thread(agent, distrm, idle_agent);

	print("created idle thread\n");

	for_each_entry(agent_t, a, &dcop->agents) {
		distrm_agent_t *_a = (distrm_agent_t *) tlm_malloc(a->tlm, sizeof(distrm_agent_t));

		_a->agent = a;

		_a->owned_cores = view_new_tlm(a->tlm);
		_a->reserved_cores = view_new_tlm(a->tlm);

		char statebuf[8];
		unsigned int seed = time(NULL) / a->id;
		initstate_r(seed, statebuf, sizeof(statebuf), &_a->buf);
		srandom_r(seed, &_a->buf);

		double *param;
		if ((param = distrm_retrieve_downey_params(dcop, a->id))) {
			print("retrieved downey parameters from lua configuration\n");

			_a->A = param[0];
			_a->sigma = param[1];

			free(param);
		} else {
			_a->A = 20 + random_d(_a) * 280;
			_a->sigma = random_d(_a) * 2.5;
		}

		for_each_entry(resource_t, r, &a->view->resources) {
			if (agent_is_owner(a, r)) {
				view_add_resource(_a->owned_cores, resource_clone(r));

				cluster_register_core(_a, r);
			}
		}

		_a->stale = false;

		agent_create_thread(a, distrm, _a);

		print("agent %i: A %lf sigma %lf\n", a->id, _a->A, _a->sigma);
	}
}

static void distrm_cleanup(dcop_t *dcop) {
	view_t *system = cluster_unload();

	for_each_entry(agent_t, a, &dcop->agents) {
		distrm_agent_t *_a = (distrm_agent_t *) agent_cleanup_thread(a);

		resource_free(_a->core);

		view_free(_a->owned_cores);
		view_free(_a->reserved_cores);

		tlm_free(a->tlm, _a);

		view_copy(a->view, system);
	}

	view_free(system);

	agent_cleanup_thread(idle_agent->agent);
	agent_free(idle_agent->agent);

	pthread_mutex_destroy(&distrm_m);
	pthread_cond_destroy(&distrm_cv);
}

static void distrm_run(dcop_t *dcop) {
	agent_broadcast(NULL, dcop, distrm_message_new(NULL, DISTRM_START));

	//agent_send(NULL, idle_agent->agent, distrm_message_new(NULL, DISTRM_START));
}

static void distrm_kill(dcop_t *dcop) {
	agent_broadcast(NULL, dcop, distrm_message_new(NULL, DISTRM_END));

	agent_send(NULL, idle_agent->agent, distrm_message_new(NULL, DISTRM_END));

	cluster_stop();
}

void distrm_register() {
	_distrm = algorithm_new("distrm", distrm_init, distrm_cleanup, distrm_run, distrm_kill, distrm_usage);
	dcop_register_algorithm(&_distrm);
}

