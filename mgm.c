#include <pthread.h>
#include <stdlib.h>

#include "dcop.h"

typedef struct {
	pthread_t tid;
	pthread_cond_t cv;
	pthread_mutex_t mt;
	mgm_message buf;
	int term;
	bool can_move;
	double improve;
	resource *new_view;
	agent *agent;
} mgm_agent;

typedef struct {
	enum {
		MGM_OK,
		MGM_IMPROVE,
		MGM_END
	} type;
	agent *origin;
	union {
		resource *view;
		struct {
			double eval;
			double improve;
			int term;
		};
	};
} mgm_message;

mgm_message * mgm_receive(mgm_agent *agent) {
	pthread_mutex_lock(&agent->mt);
	pthread_cond_wait(&agent->cv, &agent->mt);

	mgm_message *msg = (mgm_message *) malloc(sizeof(mgm_message));
	memcpy(msg, &agent->buf, sizeof(mgm_message));

	pthread_mutex_unlock(&agent->mt);

	return msg;
}

void mgm_send(mgm_agent *agent, agent *rec, mgm_message *msg) {
	msg->origin = agent->agent;

	mgm_agent *_rec;
	for_each_entry(agents, _rec) {
		if (_rec->agent->id == rec->id) break;
	}

	pthread_mutex_lock(&_rec->mt);

	memcpy(&_rec->buf, msg, sizeof(mgm_message));

	pthread_cond_signal(&_rec->cv);

	pthread_mutex_unlock(&_rec->mt);
}

static int agents_n = 0;
static mgm_agent **agents = NULL;

static int max_distance;

#define min(x, y) (x < y ? x : y)

int send_ok(mgm_agent *a) {
	a->term++;
	if (a->term == max_distance) {
		agent *n;
		for_each_entry(a->agent->neighbors, n) {
			msg_message msg = (mgm_message) { .type = MGM_END };
			mgm_send(a, n, &msg);
		}

		return -1;
	}
	if (a->can_move) {
		memcpy(a->agent->view, a->new_view, a->agent->view_n * sizeof(resource));
	}
	agent *n;
	for_each_entry(a->agent->neighbors, n) {
		msg_message msg = (mgm_message) { .type = MGM_OK, .view = a->agent->view };
		mgm_send(a, n, &msg);
	}

	return 0;
}

void improve(mgm_agent *a) {
	// ...
}

void send_improve(mgm_agent *a) {
	double eval = agent_evaluate(a->agent);
	// calculate max_improvement and corresponding value
	improve(a);
	if (a->improve > 0) {
		a->can_move = true;
	} else {
		a->can_move = false;
	}
	agent *n;
	for_each_entry(a->agent->neighbors, n) {
		msg_message msg = (mgm_message) { .type = MGM_IMPROVE, .eval = eval, .improve = a->improve, .term = a->term };
		mgm_send(a, n, &msg);
	}
}

void * mgm(void *arg) {
	mgm_agent *agent = (mgm_agent *) arg;

	bool stop = false;
	int counter = 0;
	double improve = 0;
	bool consistent = true;
	while (!stop) {
		mgm_message *msg = mgm_receive(agent);

		switch(msg->type) {
			case MGM_OK:
				counter++;
				agent_merge_view(agent->agent, msg->view);
				if (counter == agent->agent->neighbors_n) {
					send_improve(agent);
					counter = 0;
				}
				break;

			case MGM_IMPROVE:
				counter++;
				agent->term = min(agent->term, msg->term);
				if (msg->improve > improve || msg->improve == improve && agent->agent->id > msg->origin->id) {
					agent->can_move = false;
				}
				if (msg->eval > 0) consistent = false;
				if (counter == agent->agent->neighbors_n) {
					if (send_ok(agent)) {
						stop = true;
						break;
					}
					agent_clear_view(agent->agent);
					counter = 0;
				}
				break;

			case MGM_END:
				stop = true;
				break;
		}

		free(msg);
	}

	pthread_exit(NULL);
}

void init(dcop *dcop) {

}
