#ifndef DISTRM_H_
#define DISTRM_H_

#include <stdbool.h>
#include <stdlib.h>

typedef struct distrm_agent distrm_agent_t;

#include "agent.h"
#include "dcop.h"
#include "region.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

struct distrm_agent {
	agent_t *agent;
	struct random_data buf;
	resource_t *core;
	double A;
	double sigma;
	view_t *owned_cores;
	view_t *reserved_cores;
};

typedef struct distrm_message {
	enum {
		DISTRM_REGISTER,
		DISTRM_LOCATE,
		DISTRM_INFO,
		DISTRM_REQUEST,
		DISTRM_OFFER,
		DISTRM_FORWARD,
		DISTRM_MAKE_OFFER,
		DISTRM_ACCEPT,
		DISTRM_REJECT,
		DISTRM_START,
		DISTRM_END
	} type;
	/*
	union {
		struct {
			agent_t *agent;
			resource_t *core;
			int index;
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
	*/
	agent_t *agent;
	resource_t *core;
	int index;
	region_t *region;
	distrm_agent_t *from;
	int num_neighbors;
	agent_t **neighbors;
	view_t *offer;
} distrm_message_t;

message_t * distrm_message_new(tlm_t *tlm, int type);

void distrm_message_free(tlm_t *tlm, void *buf);

#define distrm_message(m) ((distrm_message_t *) m->buf)

agent_t * distrm_get_agent(dcop_t *dcop, int id);

bool distrm_is_idle_agent(int id);

resource_t * distrm_get_random_core(distrm_agent_t *a);

void distrm_register();

#endif /* DISTRM_H_ */

