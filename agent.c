#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "agent.h"
#include "constraint.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"
#include "view.h"

#include <sim_api.h>

agent_t * agent_new() {
	agent_t *a = (agent_t *) calloc(1, sizeof(agent_t));

	pthread_mutex_init(&a->mt, NULL);
	pthread_cond_init(&a->cv, NULL);

	INIT_LIST_HEAD(&a->msg_queue);
	INIT_LIST_HEAD(&a->constraints);
	INIT_LIST_HEAD(&a->neighbors);

	return a;
}

void agent_free(agent_t *a) {
	if (a) {
		for_each_entry_safe(message_t, m, _m, &a->msg_queue) {
			list_del(&m->_l);
			message_free(m);
		}

		view_free(a->view);

		if (a->agent_view) {
			for (int i = 0, n = 0; n < a->number_of_neighbors; i++) {
				if (a->agent_view[i]) {
					n++;
					view_free(a->agent_view[i]);
				}
			}

			free(a->agent_view);
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

		free(a);
	}
}

neighbor_t * neighbor_new(agent_t *a) {
	neighbor_t *n = (neighbor_t *) calloc(1, sizeof(neighbor_t));
	n->agent = a;

	return n;
}

message_t * message_new(void *buf, void (*free)(void *)) {
	message_t *msg = (message_t *) calloc(1, sizeof(message_t));

	msg->buf = buf;
	msg->free = free;

	return msg;
}

void agent_load(dcop_t *dcop, agent_t *a) {
	//a->dcop = dcop;

	lua_getfield(dcop->L, -1, "id");
	a->id = lua_tonumber(dcop->L, -1);

	lua_getfield(dcop->L, -2, "view");
	a->view = view_new();
	view_load(dcop, a->view);

	lua_pop(dcop->L, 2);

	lua_getglobal(dcop->L, "__agents");
	lua_pushvalue(dcop->L, -2);
	a->ref = luaL_ref(dcop->L, -2);
	lua_pop(dcop->L, 2);

	a->number_of_neighbors = 0;
}

static bool agent_is_neighbor(agent_t *a, agent_t *b) {
	for_each_entry(neighbor_t, n, &a->neighbors) {
		if (n->agent->id == b->id) return true;
	}

	return false;
}

void agent_load_neighbors(dcop_t *dcop, agent_t *a) {
	lua_getfield(dcop->L, -1, "neighbors");

	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		int id = lua_tonumber(dcop->L, -1);
		agent_t *n = dcop_get_agent(dcop, id);
		if (!agent_is_neighbor(a, n)) {
			list_add_tail(&neighbor_new(n)->_l, &a->neighbors);
			a->number_of_neighbors++;
		}
		if (!agent_is_neighbor(n, a)) {
			list_add_tail(&neighbor_new(a)->_l, &n->neighbors);
			n->number_of_neighbors++;
		}

		lua_pop(dcop->L, 1);
	}

	lua_pop(dcop->L, 1);

	lua_getfield(dcop->L, -1, "agent_view");
	a->agent_view = (view_t **) realloc(a->agent_view, (dcop->number_of_agents + 1) * sizeof(view_t *));
	memset(a->agent_view, 0, (dcop->number_of_agents + 1) * sizeof(view_t *));
	for (int i = 1; i <= dcop->number_of_agents; i++) {
		if (agent_is_neighbor(a, dcop_get_agent(dcop, i))) {
			lua_pushnumber(dcop->L, i);
			lua_gettable(dcop->L, -2);
			a->agent_view[i] = view_new();
			view_load(dcop, a->agent_view[i]);
			lua_pop(dcop->L, 1);
		}
	}
	
	lua_pop(dcop->L, 1);
}

void agent_load_constraints(dcop_t *dcop, agent_t *a) {
	a->has_native_constraints = false;
	a->has_lua_constraints = false;

	lua_getfield(dcop->L, -1, "constraints");

	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		constraint_t *c = constraint_new();
		constraint_load(dcop, c);
		list_add_tail(&c->_l, &a->constraints);

		if (c->type == CONSTRAINT_TYPE_NATIVE) {
			a->has_native_constraints = true;
		} else {
			a->has_lua_constraints = true;
		}
	}

	lua_pop(dcop->L, 1);
}

void agent_update_view(agent_t *agent, view_t *new) {
	view_free(agent->view);
	agent->view = new;
}

void agent_clear_view(agent_t *agent) {
	for_each_entry(neighbor_t, n, &agent->neighbors) {
		view_clear(agent->agent_view[n->agent->id]);
	}
}

double agent_evaluate(dcop_t *dcop, agent_t *agent) {
	double r = 0;

	SimRoiEnd();

	pthread_mutex_lock(&dcop->mt);

	dcop_refresh_agents(dcop);

	if (!agent->has_native_constraints) {
		lua_getglobal(dcop->L, "__agents");
		lua_rawgeti(dcop->L, -1, agent->ref);
		lua_getfield(dcop->L, -1, "rate_view");
		lua_pushvalue(dcop->L, -2);
		if (lua_pcall(dcop->L, 1, 1, 0)) {
			printf("error: failed to call function 'rate_view' for agent %i (%s)\n", agent->id, lua_tostring(dcop->L, -1));
			lua_pop(dcop->L, 3);

			r = INFINITY;
		} else {
			r = lua_tonumber(dcop->L, -1);

			lua_pop(dcop->L, 3);
		}
	} else {
		for_each_entry(constraint_t, c, &agent->constraints) {
			r += c->eval(c);
		}
	}

	pthread_mutex_unlock(&dcop->mt);

	SimRoiStart();

	return r;
}

void agent_send(agent_t *s, agent_t *r, message_t *msg) {
	msg->from = s;

	pthread_mutex_lock(&r->mt);

	list_add_tail(&msg->_l, &r->msg_queue);

	pthread_cond_signal(&r->cv);

	pthread_mutex_unlock(&r->mt);
}

message_t * agent_recv(agent_t *r) {
	pthread_mutex_lock(&r->mt);

	if (list_empty(&r->msg_queue)) pthread_cond_wait(&r->cv, &r->mt);

	message_t *msg = list_first_entry(&r->msg_queue, message_t, _l);
	list_del(&msg->_l);

	pthread_mutex_unlock(&r->mt);

	return msg;
}

message_t * agent_recv_filter(agent_t *r, bool (*filter)(message_t *, void *), void *arg) {
	pthread_mutex_lock(&r->mt);

	if (list_empty(&r->msg_queue)) pthread_cond_wait(&r->cv, &r->mt);

	message_t *msg = NULL;

	while (!msg) {
		for_each_entry(message_t, m, &r->msg_queue) {
			if (filter(m, arg)) {
				msg = m;
				break;
			}
		}

		if (!msg) pthread_cond_wait(&r->cv, &r->mt);
	}

	list_del(&msg->_l);

	pthread_mutex_unlock(&r->mt);

	return msg;
}

void agent_refresh(dcop_t *dcop, agent_t *a) {
	pthread_mutex_lock(&dcop->mt);

	for_each_entry(resource_t, r, &a->view->resources) {
		resource_refresh(dcop, r);
	}

	for_each_entry(neighbor_t, n, &a->neighbors) {
		for_each_entry(resource_t, r, &a->agent_view[n->agent->id]->resources) {
			resource_refresh(dcop, r);
		}
	}

	pthread_mutex_unlock(&dcop->mt);
}

int agent_create_thread(agent_t *a, void * (*algorithm)(void *), void *arg) {
	return pthread_create(&a->tid, NULL, algorithm, arg);
}

void * agent_cleanup_thread(agent_t *a) {
	void *ret;

	pthread_join(a->tid, &ret);

	return ret;
}

void agent_dump_view(agent_t *a) {
	printf("[%i] agent_view[0]:\n", a->id);
	view_dump(a->view);
	for_each_entry(neighbor_t, n, &a->neighbors) {
		printf("[%i] agent_view[%i]:\n", a->id, n->agent->id);
		view_dump(a->agent_view[n->agent->id]);
	}
}
