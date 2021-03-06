#define _GNU_SOURCE

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "agent.h"
#include "console.h"
#include "constraint.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"
#include "view.h"

#include <sim_api.h>

#define TLM_SIZE 1024 // KBytes

bool use_tlm = true;

agent_t * agent_new() {
	//agent_t *a = (agent_t *) calloc(1, sizeof(agent_t));
	// allocate agents page-aligned, so that coherency traffic is only caused by message passing
	// TODO: hm... might actually not be necessary (cache lines are pretty small)
	//agent_t *a = dcop_malloc_aligned(sizeof(agent_t));

	tlm_t *tlm = NULL;

	if (use_tlm) {
		tlm = tlm_create(TLM_SIZE);
	}

	agent_t *a = (agent_t *) tlm_malloc(tlm, sizeof(agent_t));

	pthread_mutex_init(&a->mt, NULL);
	pthread_cond_init(&a->cv, NULL);

	INIT_LIST_HEAD(&a->msg_queue);
	INIT_LIST_HEAD(&a->constraints);
	INIT_LIST_HEAD(&a->neighbors);

	a->tlm = tlm;

	a->bytes_sent = 0;

	return a;
}

void agent_free(agent_t *a) {
	if (a) {
		// TODO: can't free messages because they're possibly allocated in a tlm of an
		// agent that is already free'd and hence has a destroyed tlm
		// doesn't really matter though, since tlm_destroy free's up all memory anyway
		// tlm_free only makes space for more tlm_malloc's
		/*
		for_each_entry_safe(message_t, m, _m, &a->msg_queue) {
			list_del(&m->_l);
			message_free(m);
		}
		*/

		// FIXED: Only free messages if we're not using TLM; When using TLM messages get free'd automatically with tlm_destroy
		if (!use_tlm) {
			for_each_entry_safe(message_t, m, _m, &a->msg_queue) {
				list_del(&m->_l);
				message_free(m);
			}
		}

		view_free(a->view);

		if (a->agent_view) {
			for (int i = 0, n = 0; n < a->number_of_neighbors; i++) {
				if (a->agent_view[i]) {
					n++;
					view_free(a->agent_view[i]);
				}
			}

			tlm_free(a->tlm, a->agent_view);
		}

		for_each_entry_safe(neighbor_t, n, _n, &a->neighbors) {
			list_del(&n->_l);
			neighbor_free(n);
		}

		for_each_entry_safe(constraint_t, c, _c, &a->constraints) {
			list_del(&c->_l);
			constraint_free(c);
		}

		pthread_mutex_destroy(&a->mt);
		pthread_cond_destroy(&a->cv);

		if (a->L) {
			lua_close(a->L);
		}

		tlm_destroy(a->tlm);

		//free(a);
	}
}

neighbor_t * neighbor_new(tlm_t *tlm, agent_t *a) {
	//neighbor_t *n = (neighbor_t *) calloc(1, sizeof(neighbor_t));
	neighbor_t *n = (neighbor_t *) tlm_malloc(tlm, sizeof(neighbor_t));
	n->agent = a;

	n->tlm = tlm;

	return n;
}

message_t * message_new(tlm_t *tlm, void *buf, size_t size, void (*free)(tlm_t *, void *)) {
	message_t *msg = (message_t *) tlm_malloc(tlm, sizeof(message_t));

	msg->buf = buf;
	msg->size = size;
	msg->free = free;

	msg->tlm = tlm;

	return msg;
}

void agent_load(dcop_t *dcop, agent_t *a) {
	a->dcop = dcop;

	lua_getfield(dcop->L, -1, "id");
	a->id = lua_tonumber(dcop->L, -1);

	lua_pop(dcop->L, 2);

	a->number_of_neighbors = 0;
}

void agent_load_view(agent_t *a) {
	lua_getfield(a->L, -1, "view");
	a->view = view_new_tlm(a->tlm);
	view_load(a->L, a->view);

	lua_pop(a->L, 1);
}

static bool agent_is_neighbor(agent_t *a, agent_t *b) {
	for_each_entry(neighbor_t, n, &a->neighbors) {
		if (n->agent->id == b->id) {
			return true;
		}
	}

	return false;
}

void agent_load_neighbors(agent_t *a) {
	dcop_t *dcop = a->dcop;

	lua_getfield(dcop->L, -1, "neighbors");

	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		int id = lua_tonumber(dcop->L, -1);
		agent_t *n = dcop_get_agent(dcop, id);

		if (!agent_is_neighbor(a, n)) {
			list_add_tail(&neighbor_new(a->tlm, n)->_l, &a->neighbors);
			a->number_of_neighbors++;
		}
		if (!agent_is_neighbor(n, a)) {
			list_add_tail(&neighbor_new(n->tlm, a)->_l, &n->neighbors);
			n->number_of_neighbors++;
		}

		lua_pop(dcop->L, 1);
	}

	lua_pop(dcop->L, 1);
}

void agent_load_agent_view(agent_t *a) {
	dcop_t *dcop = a->dcop;

	lua_getfield(a->L, -1, "agent_view");

	//a->agent_view = (view_t **) realloc(a->agent_view, (dcop->number_of_agents + 1) * sizeof(view_t *));
	//memset(a->agent_view, 0, (dcop->number_of_agents + 1) * sizeof(view_t *));
	a->agent_view = (view_t **) tlm_malloc(a->tlm, (dcop->number_of_agents + 1) * sizeof(view_t *));

	for (int i = 1; i <= dcop->number_of_agents; i++) {
		if (agent_is_neighbor(a, dcop_get_agent(dcop, i))) {
			lua_pushnumber(a->L, i);
			lua_gettable(a->L, -2);

			a->agent_view[i] = view_new_tlm(a->tlm);
			view_load(a->L, a->agent_view[i]);

			lua_pop(a->L, 1);
		}
	}
	
	lua_pop(a->L, 1);
}

void agent_load_constraints(agent_t *a) {
	a->has_native_constraints = false;
	a->has_lua_constraints = false;

	lua_newtable(a->L);
	lua_setglobal(a->L, "__constraints");

	lua_getfield(a->L, -1, "constraints");

	int t = lua_gettop(a->L);
	lua_pushnil(a->L);
	while (lua_next(a->L, t)) {
		constraint_t *c = constraint_new(a->tlm);
		constraint_load(a, c);

		list_add_tail(&c->_l, &a->constraints);
	}

	lua_pop(a->L, 1);
}

void agent_clear_agent_view(agent_t *a) {
	for_each_entry(neighbor_t, n, &a->neighbors) {
		view_clear(a->agent_view[n->agent->id]);
	}
}

double agent_evaluate(agent_t *a) {
	double r = 0;

	if (skip_lua && a->has_lua_constraints) {
		//console_disable();
		//SimRoiEnd();
		// TODO: should we actually do that? manual says there can only be a single (continuous?) ROI
		SimSetInstrumentMode(SIM_OPT_INSTRUMENT_WARMUP);
		//console_enable();
	}

	if (a->has_lua_constraints) {
		agent_refresh(a);
	}

	if (!a->has_native_constraints) {
		lua_getglobal(a->L, "__agent");
		lua_getfield(a->L, -1, "rate_view");
		lua_pushvalue(a->L, -2);
		if (lua_pcall(a->L, 1, 1, 0)) {
			print_error("failed to call function 'rate_view' for agent %i (%s)\n", a->id, lua_tostring(a->L, -1));
			lua_pop(a->L, 2);

			r = INFINITY;
		} else {
			r = lua_tonumber(a->L, -1);

			lua_pop(a->L, 2);
		}
	} else {
		for_each_entry(constraint_t, c, &a->constraints) {
			r += c->eval(c);

			// IMPROVEMENT: do not evaluate further constraints if already INF
			if (!isfinite(r)) {
				break;
			}

#ifdef DEBUG_NATIVE_CONSTRAINTS
			console_lock();
			agent_refresh(a);
			double _r = c->eval(c);
			double __r = constraint_evaluate_lua(c);
			if (_r != __r) {
				print_error("native constraint '%s' different from lua version: %f vs %f\n", c->name, _r , __r);
			}
			console_unlock();
#endif
		}
	}

	if (skip_lua && a->has_lua_constraints) {
		//SimRoiStart();
		//console_disable();
		SimSetInstrumentMode(SIM_OPT_INSTRUMENT_DETAILED);
		//console_enable();
	}

	return r;
}

double agent_evaluate_view(agent_t *a, view_t *v) {
	view_t *_view = a->view;

	a->view = v;

	double eval = agent_evaluate(a);

	a->view = _view;

	return eval;
}

void agent_send(agent_t *s, agent_t *r, message_t *msg) {
	msg->from = s;

	if (s) {
		s->bytes_sent += msg->size;
	}

	pthread_mutex_lock(&r->mt);

	list_add_tail(&msg->_l, &r->msg_queue);

	pthread_cond_signal(&r->cv);

	pthread_mutex_unlock(&r->mt);
}

void agent_broadcast(agent_t *s, dcop_t *dcop, message_t *msg) {
	for_each_entry(agent_t, r, &dcop->agents) {
		void *buf = tlm_malloc(msg->tlm, msg->size);
		memcpy(buf, msg->buf, msg->size);

		message_t *m = message_new(msg->tlm, buf, msg->size, msg->free);

		agent_send(s, r, m);
	}

	message_free(msg);
}

message_t * agent_recv(agent_t *r) {
	pthread_mutex_lock(&r->mt);

	if (list_empty(&r->msg_queue)) {
		pthread_cond_wait(&r->cv, &r->mt);
	}

	message_t *msg = list_first_entry(&r->msg_queue, message_t, _l);
	list_del(&msg->_l);

	pthread_mutex_unlock(&r->mt);

	return msg;
}

message_t * agent_recv_filter(agent_t *r, bool (*filter)(message_t *, void *), void *arg) {
	pthread_mutex_lock(&r->mt);

	if (list_empty(&r->msg_queue)) {
		pthread_cond_wait(&r->cv, &r->mt);
	}

	message_t *msg = NULL;

	while (!msg) {
		for_each_entry(message_t, m, &r->msg_queue) {
			if (filter(m, arg)) {
				msg = m;
				break;
			}
		}

		if (!msg) {
			pthread_cond_wait(&r->cv, &r->mt);
		}
	}

	list_del(&msg->_l);

	pthread_mutex_unlock(&r->mt);

	return msg;
}

void agent_refresh(agent_t *a) {
	if (!a->L) {
		return;
	}

	for_each_entry(resource_t, r, &a->view->resources) {
		resource_refresh(a->L, r);
	}

	for_each_entry(neighbor_t, n, &a->neighbors) {
		for_each_entry(resource_t, r, &a->agent_view[n->agent->id]->resources) {
			resource_refresh(a->L, r);
		}
	}
}

int agent_create_thread(agent_t *a, void * (*algorithm)(void *), void *arg) {
	if (a->id >= dcop_get_number_of_cores()) {
		print_warning("creating more threads than physical cores available (sniper will most likely crash)\n");
	}

	int r = pthread_create(&a->tid, NULL, algorithm, arg);
	if (!r) {
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET((a->id) % dcop_get_number_of_cores(), &cpuset);
		print_debug("pinned agent %i to core %i\n", a->id, (a->id) % dcop_get_number_of_cores());
		if (pthread_setaffinity_np(a->tid, sizeof(cpu_set_t), &cpuset)) {
			print_warning("failed to set core affinity for agent %i\n", a->id);
		}
	}

	return r;
}

void * agent_cleanup_thread(agent_t *a) {
	void *ret;

	pthread_join(a->tid, &ret);

	return ret;
}

void agent_dump_view(agent_t *a) {
	print("[%i] agent_view[0]:\n", a->id);
	view_dump(a->view);
	for_each_entry(neighbor_t, n, &a->neighbors) {
		print("[%i] agent_view[%i]:\n", a->id, n->agent->id);
		view_dump(a->agent_view[n->agent->id]);
	}
}

int agent_has_conflicting_view(agent_t *a, int id) {
	int n = 0;

	for_each_entry(resource_t, r, &a->view->resources) {
		resource_t *_r = view_get_resource(a->agent_view[id], r->index);

		if (_r && agent_is_owner(a, r) && resource_get_owner(_r) == id) {
			n++;
		}
	}

	return n;
}

