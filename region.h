#ifndef REGION_H_
#define REGION_H_

#include <stdbool.h>

#include "distrm.h"
#include "list.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

typedef struct region {
	struct list_head _l;
	resource_t *center;
	int size;
	view_t *view;
	tlm_t *tlm;
} region_t;

region_t * region_new(distrm_agent_t *a, int size);

void region_free(region_t *region);

bool region_is_unique(region_t *region, struct list_head *list);

int region_distance(region_t *region, distrm_agent_t *a);

bool region_contains(region_t *region, resource_t *r);

struct list_head * region_split(region_t *region, int size);

void region_free_all(distrm_agent_t *a, struct list_head *list);

struct list_head * region_select_random(distrm_agent_t *a, int size, int num);

region_t * region_get_most_distant(distrm_agent_t *a, struct list_head *list);

#endif /* REGION_H_ */

