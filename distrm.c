#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "agent.h"
#include "algorithm.h"
#include "console.h"
#include "dcop.h"
#include "list.h"
#include "native.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

typedef struct distrm_agent {
	agent_t *agent;
	struct random_data buf;
	resource_t *core;
	int A;
	double sigma;
	view_t *owned_cores;
	view_t *reserved_cores;
} distrm_agent_t;

typedef struct region {
	struct list_head _l;
	resource_t *center;
	int size;
	view_t *view;
	tlm_t *tlm;
} region_t;

typedef struct cluster {
	struct list_head _l;
	int size;
	view_t *view;
	agent_t *directory;
} cluster_t;

typedef struct distrm_message {
	enum {
		DISTRM_REGISTER,
		DISTRM_LOCATE,
		DISTRM_INFO,
		DISTRM_REQUEST,
		DISTRM_OFFER,
		DISTRM_FORWARD,
		DISTRM_MAKE_OFFER,
		DISTRM_END
	} type;
	union {
		struct {
			agent_t *agent;
			resource_t *core;
		};
		struct {
			region_t *region;
			distrm_agent_t *from;
			int num_neighbors;
			agent_t **neighbors;
		};
		struct {
			view_t *offer;
		};
	};
} distrm_message_t;

static algorithm_t _distrm;

static int cluster_size = 2;
static int try_limit = 5;
static int max_par_reqs = 1;
static int region_size = 2;
static int region_num = 3;
static int max_dist = 2;
static int locality_thresh = 1;
static int size_thresh = 10;

static LIST_HEAD(clusters);

static distrm_agent_t *idle_agent = NULL;

#define distrm_message(m) ((distrm_message_t *) m->buf)

static void distrm_message_free(tlm_t *tlm, void *buf) {
	distrm_message_t *msg = (distrm_message_t *) buf;

	if (msg) {
		tlm_free(tlm, msg);
	}
}

static message_t * distrm_message_new(tlm_t *tlm, int type) {
	distrm_message_t *msg = (distrm_message_t *) tlm_malloc(tlm, sizeof(distrm_message_t));

	msg->type = type;

	message_t *m = message_new(tlm, msg, distrm_message_free);

	return m;
}

static resource_t * get_random_core(distrm_agent_t *a) {
	int index;
	random_r(&a->buf, &index);
	index %= a->agent->dcop->hardware->number_of_resources;

	return resource_clone(view_get_resource(a->agent->view, index));
}

static region_t * region_new(distrm_agent_t *a, int size) {
	region_t *region = tlm_malloc(a->agent->tlm, sizeof(region_t));

	region->tlm = a->agent->tlm;

	// actually... I think we should be able to select a region around our initial core
	do {
		region->center = get_random_core(a);
	} while (region->center->index == a->core->index);

	region->view = view_new_tlm(region->tlm);

	region->size = 0;

	// it would be easier to iterate over all resources and compare the distance between the indices...
	resource_t *prev = list_entry(region->center->_l.prev, resource_t, _l);
	resource_t *next = list_entry(region->center->_l.next, resource_t, _l);
	for (int i = 0; i < size; i++) {	
		bool grew = false;

		if (prev->index != region->center->index && &prev->_l != &a->agent->view->resources) {
			view_add_resource(region->view, resource_clone(prev));
			grew = true;

			prev = list_entry(prev->_l.prev, resource_t, _l);
		}
		if (next->index != region->center->index && &next->_l != &a->agent->view->resources) {
			view_add_resource(region->view, resource_clone(next));
			grew = true;

			next = list_entry(next->_l.next, resource_t, _l);
		}

		if (grew) {
			size++;
		} else {
			break;
		}
	}

	view_add_resource(region->view, region->center);
	size++;

	return region;
}

static void region_free(region_t *region) {
	view_free(region->view);

	tlm_free(region->tlm, region);
}

static bool region_is_unique(region_t *region, struct list_head *list) {
	for_each_entry(region_t, r, list) {
		if (region->center->index == r->center->index) {
			return false;
		}
	}

	return true;
}

static int region_distance(region_t *region, distrm_agent_t *a) {
	return  abs(region->center->index - a->core->index);
}

static bool region_contains(region_t *region, resource_t *r) {
	return (abs(region->center->index - r->index) <= region->size);
}

static struct list_head * region_split(region_t *region, int size) {
	struct list_head *subregions = tlm_malloc(region->tlm, sizeof(struct list_head));
	INIT_LIST_HEAD(subregions);

	int num_subregions = ceil((double) region->size / size);

	for (int i = 0; i < num_subregions; i++) {
		region_t *subregion = tlm_malloc(region->tlm, sizeof(region_t));
		subregion->tlm = region->tlm;

		int j = 0;
		for_each_entry(resource_t, r, &region->view->resources) {
			view_del_resource(region->view, r);
			view_add_resource(subregion->view, r);

			j++;

			if (!subregion->center && j == size) {
				subregion->center = r;
				j = 0;
			} else if (j == size) {
				break;
			}
		}

		if (!subregion->center) {
			subregion->center = list_entry(subregion->view->resources.next, resource_t, _l);
			subregion->size = j - 1;
		} else {
			subregion->size = size;
		}

		list_add_tail(&subregion->_l, subregions);
	}

	return subregions;
}

static struct list_head * select_random_regions(distrm_agent_t *a, int size, int num) {
	struct list_head *list = tlm_malloc(a->agent->tlm, sizeof(struct list_head));
	INIT_LIST_HEAD(list);

	for (int i = 0; i < num; i++) {
		region_t *r = region_new(a, size);

		if (region_is_unique(r, list)) {
			list_add_tail(&r->_l, list);
		} else {
			region_free(r);
			i--;
		}
	}

	return list;
}

static region_t * get_most_distant_region(distrm_agent_t *a, struct list_head *list) {
	region_t *region = NULL;

	for_each_entry(region_t, r, list) {
		if (!region || region_distance(region, a) < region_distance(r, a)) {
			region = r;
		}
	}

	return region;
}

static double get_random_double(distrm_agent_t *a) {
	int i;
	random_r(&a->buf, &i);

	return (double) i / RAND_MAX;
}

static double speedup(distrm_agent_t *a, view_t *v) {
	return _downey(a->A, a->sigma, v->size);
}

static view_t * create_offer(distrm_agent_t *a, distrm_agent_t *c, region_t *region) {
	view_t *offered_cores = view_new_tlm(a->agent->tlm);

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

		for_each_entry(resource_t, r, &potential_cores->resources) {
			view_add_resource(cores_receiver, r);

			resource_t  *_r = view_get_resource(cores_giver, r->index);
			view_del_resource(cores_giver, _r);

			double gain_receiver = share_giver * speedup(c, cores_receiver) - base_receiver;
			double loss_giver = base_giver - speedup(a, cores_giver);

			if (gain_receiver - loss_giver > gain_total) {
				view_add_resource(greedy_choice, r);

				gain_total = gain_receiver - loss_giver;
			} else {
				view_del_resource(cores_receiver, r);
				view_add_resource(potential_cores, r);

				view_add_resource(cores_giver, _r);
			}
		}

		if (gain_total > 0) {
			view_concat(offered_cores, greedy_choice);

			view_cut(potential_cores, offered_cores);

			view_concat(cores_receiver, offered_cores);
			view_cut(cores_giver, offered_cores);
		}

		view_free(greedy_choice);
	}

	view_free(cores_giver);
	view_free(cores_receiver);

	view_free(potential_cores);

	view_cut(a->owned_cores, offered_cores);
	view_concat(a->reserved_cores, offered_cores);

	return offered_cores;
}

static  bool cluster_contains(cluster_t *c, resource_t *r) {
	return (view_get_resource(c->view, r->index) != NULL);
}

static agent_t * cluster_get_directory(resource_t *r) {
	for_each_entry(cluster_t, c, &clusters) {
		if (cluster_contains(c, r)) {
			return c->directory;
		}
	}

	return NULL;
}

static bool filter_distrm_info(message_t *msg, void *core) {
	if (distrm_message(msg)->type == DISTRM_END) {
		return true;
	}

	return (distrm_message(msg)->type == DISTRM_INFO && distrm_message(msg)->core == (resource_t *) core);
}

static agent_t * resolve_core_owner(distrm_agent_t *a,  resource_t *r) {
	agent_t *directory = cluster_get_directory(r);

	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_LOCATE);
	distrm_message(msg)->core = r;
	agent_send(a->agent, directory, msg);

	msg = agent_recv_filter(a->agent, filter_distrm_info, r);

	agent_t *agent = distrm_message(msg)->agent;

	message_free(msg);

	return agent;
}

static void register_core(distrm_agent_t *a, resource_t *r) {
	agent_t *directory = cluster_get_directory(r);

	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REGISTER);
	distrm_message(msg)->agent = a->agent;
	distrm_message(msg)->core = r;

	agent_send(a->agent, directory, msg);
}

#define distrm_get_agent(dcop, id) (id > 0 ? dcop_get_agent(dcop, id) : idle_agent->agent)

static void * directory_service(void *arg) {
	cluster_t *c = (cluster_t *) arg;

	bool stop = false;
	while (!stop) {
		message_t *msg = agent_recv(c->directory);

		resource_t *r;

		switch(distrm_message(msg)->type) {
			case DISTRM_REGISTER:
				r = view_get_resource(c->view, distrm_message(msg)->core->index);
				agent_claim_resource(distrm_message(msg)->agent, r);
				break;

			case DISTRM_LOCATE:
				r = view_get_resource(c->view, distrm_message(msg)->core->index);

				agent_t *agent = distrm_get_agent(c->directory->dcop, r->owner);

				message_t *response = distrm_message_new(c->directory->tlm, DISTRM_INFO);
				distrm_message(response)->core = r; // maybe we should use the core from the request message
				distrm_message(response)->agent = agent;
				agent_send(c->directory, msg->from, response);
				break;

			case DISTRM_END:
				stop = true;
				break;

			default:
				print("DistRM directory service received unsupport message type %i\n", distrm_message(msg)->type);
				break;
		}

		message_free(msg);
	}

	return (void *) c;
}

static void send_request(distrm_agent_t *a, region_t *region) {
	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
	distrm_message(msg)->region = region;
	distrm_message(msg)->from = a;

	agent_t *handler = resolve_core_owner(a, region->center);

	agent_send(a->agent, handler, msg);
}

static bool filter_distrm_offer(message_t *msg, void *unused) {
	if (distrm_message(msg)->type == DISTRM_END) {
		return true;
	}

	return (distrm_message(msg)->type == DISTRM_OFFER);
}

static void handle_make_offer(distrm_agent_t *a, message_t *msg) {
	message_t *response = distrm_message_new(a->agent->tlm, DISTRM_OFFER);

	distrm_message(response)->offer = view_new_tlm(a->agent->tlm);

	view_concat(distrm_message(response)->offer, create_offer(a, distrm_message(msg)->from, distrm_message(msg)->region));

	agent_send(a->agent, msg->from, response);
}

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

		view_concat(distrm_message(response)->offer, distrm_message(remote_offer)->offer);
	}

	if (a != distrm_message(msg)->from) {
		view_concat(distrm_message(response)->offer, create_offer(a, distrm_message(msg)->from, distrm_message(msg)->region));
	}

	agent_send(a->agent, distrm_message(msg)->from->agent, response);
}

static void handle_request_message(distrm_agent_t *a, message_t *msg) {
	int *cores = tlm_malloc(a->agent->tlm, (a->agent->dcop->number_of_agents + 1) * sizeof(int));

	for_each_entry(resource_t, r, &distrm_message(msg)->region->view->resources) {
		agent_t *owner = resolve_core_owner(a, r);
		cores[owner->id]++;
	}

	message_t *forward = distrm_message_new(a->agent->tlm, DISTRM_FORWARD);
	distrm_message(forward)->region = distrm_message(msg)->region;
	distrm_message(forward)->from = distrm_message(msg)->from;

	agent_t *handler = NULL;

	distrm_message(forward)->num_neighbors = 0;
	distrm_message(forward)->neighbors = NULL;

	int max = 0;
	for (int i = 0; i <= a->agent->dcop->number_of_agents; i++) {
		if (cores[i] > max) {
			max = cores[i];
			handler = distrm_get_agent(a->agent->dcop, i);
		}
		if (cores[i] > 0 && i != a->agent->id) {
			distrm_message(forward)->num_neighbors++;
			distrm_message(forward)->neighbors = tlm_realloc(a->agent->tlm, forward, distrm_message(forward)->num_neighbors);
			distrm_message(forward)->neighbors[distrm_message(forward)->num_neighbors - 1] = distrm_get_agent(a->agent->dcop, i);
		}
	}

	if (handler != a->agent && a != distrm_message(msg)->from) {
		agent_send(a->agent, handler, forward);
	} else {
		handle_request(a, forward);
	}
}

static void request_cores(distrm_agent_t *a) {
	a->core = get_random_core(a);
	print("DistRM Agent %i seeded core: %i\n", a->agent->id, a->core->index);

	view_t *offers = view_new_tlm(a->agent->tlm);

	for (int tries = 0; tries < try_limit && offers->size == 0; tries++) {
		struct list_head *potential_regions = select_random_regions(a, region_size, region_num);

		int i = region_num;
		for_each_entry_safe(region_t, region, _r, potential_regions) {
			if (region_distance(region, a) < tries * (max_dist / try_limit) && get_random_double(a) > (double) 1 / tries) {
				list_del(&region->_l);
				i--;
			}
		}

		while (i > max_par_reqs) {
			region_t *region = get_most_distant_region(a, potential_regions);
			list_del(&region->_l);
			i--;
		}

		for_each_entry(region_t, region, potential_regions) {
			if (region_distance(region, a) > locality_thresh) {
				send_request(a, region);
			} else {
				if (region->size > size_thresh) {
					struct list_head *subregions = region_split(region, size_thresh);

					for_each_entry_safe(region_t, subregion, _s, subregions) {
						send_request(a, subregion);

						list_del(&subregion->_l);
						region_free(subregion);
					}

					tlm_free(a->agent->tlm, subregions);
				} else {
					message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REQUEST);
					distrm_message(msg)->region = region;
					distrm_message(msg)->from = a;

					handle_request_message(a, msg);
				}
			}
		}

		for_each_entry_safe(region_t, r, _r, potential_regions) {
			list_del(&r->_l);
			region_free(r);
		}
		tlm_free(a->agent->tlm, potential_regions);
	}
}

static void handle_offer(distrm_agent_t *a, message_t *msg) {
	// decide what cores you wanna take
	// send back responses to owners
	// register at directory service
	register_core(a, NULL);
}

static void * distrm(void *arg) {
	distrm_agent_t *a = (distrm_agent_t *) arg;

	char statebuf[8];
	unsigned int seed = time(NULL) / a->agent->id;
	initstate_r(seed, statebuf, sizeof(statebuf), &a->buf);
	srandom_r(seed, &a->buf);

	// if we're invading agent, request cores
	if (false) {
		request_cores(a);
	}

	bool stop = false;
	while (!stop) {
		message_t *msg = agent_recv(a->agent);

		switch(distrm_message(msg)->type) {
			case DISTRM_MAKE_OFFER:
				handle_make_offer(a, msg);
				break;

			case DISTRM_OFFER:
				handle_offer(a, msg);
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

static void distrm_init(dcop_t *dcop, int argc, char **argv) {
	parse_arguments(argc, argv);

	for_each_entry(agent_t, a, &dcop->agents) {
		distrm_agent_t *_a = (distrm_agent_t *) tlm_malloc(a->tlm, sizeof(distrm_agent_t));

		_a->agent = a;

		agent_create_thread(a, distrm, _a);
	}

	// for each cluster start directory service
	agent_t *directory = agent_new();
	cluster_t *c = tlm_malloc(directory->tlm, sizeof(cluster_t));
	c->directory = directory;

	agent_create_thread(c->directory, directory_service, c);
}

static void distrm_cleanup(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		distrm_agent_t *_a = (distrm_agent_t *) agent_cleanup_thread(a);

		tlm_free(a->tlm, _a);
	}
}

static void distrm_run(dcop_t *dcop) {
}

static void distrm_kill(dcop_t *dcop) {
}

void distrm_register() {
	_distrm = algorithm_new("distrm", distrm_init, distrm_cleanup, distrm_run, distrm_kill, distrm_usage);
	dcop_register_algorithm(&_distrm);
}

