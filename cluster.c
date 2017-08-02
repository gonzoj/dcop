#include <stdbool.h>
#include <string.h>

#include "agent.h"
#include "cluster.h"
#include "console.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

static LIST_HEAD(clusters);

static bool cluster_contains(cluster_t *c, resource_t *r) {
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

agent_t * cluster_resolve_core(distrm_agent_t *a,  resource_t *r) {
	agent_t *directory = cluster_get_directory(r);

	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_LOCATE);
	distrm_message(msg)->core = r;
	agent_send(a->agent, directory, msg);

	msg = agent_recv_filter(a->agent, filter_distrm_info, r);

	agent_t *agent = distrm_message(msg)->agent;

	message_free(msg);

	return agent;
}

void cluster_register_core(distrm_agent_t *a, resource_t *r) {
	agent_claim_resource(a->agent, r);

	agent_t *directory = cluster_get_directory(r);

	message_t *msg = distrm_message_new(a->agent->tlm, DISTRM_REGISTER);
	distrm_message(msg)->agent = a->agent;
	distrm_message(msg)->core = r;

	agent_send(a->agent, directory, msg);
}

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

				print("directory %i: received register message (agent %i: core %i)\n", c->id, msg->from->id, r->index);
				break;

			case DISTRM_LOCATE:
				r = view_get_resource(c->view, distrm_message(msg)->core->index);

				print("directory %i: received locate message (agent %i: core %i)\n", c->id, msg->from->id, r->index);

				agent_t *agent = distrm_get_agent(c->directory->dcop, r->owner);

				message_t *response = distrm_message_new(c->directory->tlm, DISTRM_INFO);
				distrm_message(response)->agent = agent;
				distrm_message(response)->core = distrm_message(msg)->core;

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

int cluster_load(dcop_t *dcop, int size) {
	int n = 0;

	int i = 0;
	cluster_t *c;
	for_each_entry(resource_t, r, &dcop->hardware->view->resources) {
		if (i++ % size == 0) {
			agent_t *directory = agent_new();
			
			directory->dcop = dcop;

			n++;
			directory->id = dcop->number_of_agents + 1 + n;

			c = tlm_malloc(directory->tlm, sizeof(cluster_t));
			c->directory = directory;

			c->id = i / size;

			c->view = view_new_tlm(c->directory->tlm);
			c->size = 0;

			agent_create_thread(c->directory, directory_service, c);

			list_add_tail(&c->_l, &clusters);

			print("started directory service %i\n", c->id);	
		}
		
		resource_t *_r = resource_new_tlm(c->directory->tlm);
		memcpy(_r, r, sizeof(resource_t));
		_r->tlm = c->directory->tlm;
		_r->type = tlm_strdup(_r->tlm, r->type);

		view_add_resource(c->view, _r);
		c->size++;
	}

	return n;
}

view_t * cluster_unload() {
	view_t *system = view_new();

	for_each_entry_safe(cluster_t, c, _c, &clusters) {
		agent_cleanup_thread(c->directory);

		for_each_entry(resource_t, r, &c->view->resources) {
			resource_t *_r = resource_new();
			memcpy(_r, r, sizeof(resource_t));
			_r->type = strdup(r->type);
			_r->tlm = NULL;
			if (distrm_is_idle_agent(_r->owner)) {
				_r->status = RESOURCE_STATUS_FREE;
			}

			view_add_resource(system, _r);
		}

		list_del(&c->_l);

		agent_free(c->directory);
	}

	return system;
}

void cluster_stop() {
	for_each_entry_safe(cluster_t, c, _c, &clusters) {
		agent_send(NULL, c->directory, distrm_message_new(NULL, DISTRM_END));
	}
}

