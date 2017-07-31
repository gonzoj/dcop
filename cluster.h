#ifndef CLUSTER_H_
#define CLUSTER_H_

#include <stdbool.h>

#include "agent.h"
#include "dcop.h"
#include "distrm.h"
#include "list.h"
#include "resource.h"
#include "view.h"

typedef struct cluster {
	struct list_head _l;
	int id;
	int size;
	view_t *view;
	agent_t *directory;
} cluster_t;

agent_t * cluster_resolve_core(distrm_agent_t *a,  resource_t *r);

void cluster_register_core(distrm_agent_t *a, resource_t *r);

int cluster_load(dcop_t *dcop, int size);

view_t * cluster_unload();

void cluster_stop();

#endif /* CLUSTER_H_ */

