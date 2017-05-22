#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>

#include "agent.h"
#include "constraint.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"

agent_t * agent_new() {
	agent_t *a = (agent_t *) calloc(1, sizeof(agent_t));

	pthread_mutex_init(&a->mt, NULL);
	pthread_cond_init(&a->cv, NULL);

	INIT_LIST_HEAD(&a->msg_queue);
	INIT_LIST_HEAD(&a->constraints);
	INIT_LIST_HEAD(&a->neighbors);
	INIT_LIST_HEAD(&a->view);

	return a;
}

void agent_free(agent_t *a) {
	if (a) {
		for_each_entry_safe(message_t, m, _m, &a->msg_queue) {
			list_del(&m->_l);
			message_free(m);
		}

		for_each_entry_safe(resource_t, r, _r, &a->view) {
			list_del(&r->_l);
			resource_free(r);
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

void agent_load(struct dcop *dcop, agent_t *a) {
	a->dcop = dcop;

	lua_getfield(dcop->L, -1, "id");
	a->id = lua_tonumber(dcop->L, -1);

	lua_getfield(dcop->L, -2, "view");
	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		resource_t *r = resource_new();
		resource_load(dcop, r);
		list_add_tail(&r->_l, &a->view);
	}
	
	lua_pop(dcop->L, 3);
}

void agent_load_neighbors(struct dcop *dcop, agent_t *a) {
	lua_getfield(dcop->L, -1, "neighbors");
	lua_pushvalue(dcop->L, -2);
	if (lua_pcall(dcop->L, 1, 1, 0)) {
		printf("error: failed to call function 'neighbors' while loading agents (%s)\n", lua_tostring(dcop->L, -1));
		lua_pop(dcop->L, 3);

		return;
	}

	a->number_of_neighbors = 0;
	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		lua_getfield(dcop->L, -1, "id");
		int id = lua_tonumber(dcop->L, -1);
		list_add_tail(&neighbor_new(dcop_get_agent(dcop, id))->_l, &a->neighbors);
		a->number_of_neighbors++;

		lua_pop(dcop->L, 2);
	}

	lua_pop(dcop->L, 1);
}

void agent_load_constraints(struct dcop *dcop, agent_t *a) {
	lua_getfield(dcop->L, -1, "constraints");

	int t = lua_gettop(dcop->L);
	lua_pushnil(dcop->L);
	while (lua_next(dcop->L, t)) {
		constraint_t *c = constraint_new();
		constraint_load(dcop, c);
		list_add_tail(&c->_l, &a->constraints);
	}

	lua_pop(dcop->L, 1);
}

void agent_merge_view(agent_t *agent, struct list_head *view) {
	for (resource_t *i = list_entry(agent->view.prev, typeof(*i), _l), 
	     *j = list_entry(view->prev, typeof(*j), _l);
	     &i->_l != &agent->view && &j->_l != view;
	     i = list_entry(i->_l.prev, typeof(*i), _l),
	     j = list_entry(j->_l.prev, typeof(*j), _l)) {
	     		if (j->status > RESOURCE_STATUS_FREE) i->status = j->status;
	     }
}

void agent_clear_view(agent_t *agent) {
	for_each_entry(resource_t, r, &agent->view) {
		r->status = RESOURCE_STATUS_UNKNOWN;
	}
}

void agent_update_view(agent_t *agent, struct list_head *new) {
	for_each_entry_safe(resource_t, r, _r, &agent->view) {
		list_del(&r->_l);
		resource_free(r);
	}

	agent->view = *new;
}

void agent_clone_view(struct list_head *view, struct list_head *clone) {
	for_each_entry(resource_t, r, view) {
		resource_t *_r = resource_new();
		memcpy(_r, r, sizeof(resource_t));
		_r->type = strdup(r->type);
		list_add_tail(&_r->_l, clone);
	}
}

void agent_free_view(struct list_head *view) {
	for_each_entry_safe(resource_t, r, _r, view) {
		list_del(&r->_l);
		resource_free(r);
	}
}

double agent_evaluate(agent_t *agent) {
	double r = 0;

	for_each_entry(constraint_t, c, &agent->constraints) {
		printf("constraint: %s\n", c->name);
		printf("evaluating something for agent %i\n", agent->id);
		r += c->eval(c);
		printf("done\n");
	}

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

void agent_refresh(dcop_t *dcop, agent_t *a) {
	for_each_entry(resource_t, r, &a->view) {
		resource_refresh(dcop, r);
	}
}

int agent_create_thread(agent_t *a, void * (*algorithm)(void *), void *arg) {
	return pthread_create(&a->tid, NULL, algorithm, arg);
}

void * agent_cleanup_thread(agent_t *a) {
	void *ret;

	pthread_join(a->tid, &ret);

	return ret;
}

