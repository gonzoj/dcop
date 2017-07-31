#include <math.h>
#include <stdbool.h>

#include "distrm.h"
#include "list.h"
#include "region.h"
#include "resource.h"
#include "tlm.h"
#include "view.h"

region_t * region_new(distrm_agent_t *a, int size) {
	region_t *region = tlm_malloc(a->agent->tlm, sizeof(region_t));

	region->tlm = a->agent->tlm;

	region->view = view_new_tlm(region->tlm);

	// actually... I think we should be able to select a region around our initial core
	region->center = distrm_get_random_core(a);

	region->size = 0;

	// it would be easier to iterate over all resources and compare the distance between the indices...
	/*
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
			region->size++;
		} else {
			break;
		}
	}

	view_add_resource(region->view, region->center);
	*/

	for_each_entry(resource_t, r, &a->agent->view->resources) {
		if (abs(r->index - region->center->index) <= size) {
			view_add_resource(region->view, resource_clone(r));
		}
	}

	region->size = size;

	return region;
}

void region_free(region_t *region) {
	view_free(region->view);

	tlm_free(region->tlm, region);
}

bool region_is_unique(region_t *region, struct list_head *list) {
	for_each_entry(region_t, r, list) {
		if (region->center->index == r->center->index) {
			return false;
		}
	}

	return true;
}

int region_distance(region_t *region, distrm_agent_t *a) {
	return  abs(region->center->index - a->core->index);
}

bool region_contains(region_t *region, resource_t *r) {
	return (abs(region->center->index - r->index) <= region->size);
}

struct list_head * region_split(region_t *region, int size) {
	struct list_head *subregions = tlm_malloc(region->tlm, sizeof(struct list_head));
	INIT_LIST_HEAD(subregions);

	int num_subregions = ceil((double) region->size / size);

	for (int i = 0; i < num_subregions; i++) {
		region_t *subregion = tlm_malloc(region->tlm, sizeof(region_t));
		subregion->tlm = region->tlm;

		subregion->view = view_new_tlm(region->tlm);

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

void region_free_all(distrm_agent_t *a, struct list_head *list) {
	if (!list) {
		return;
	}

	for_each_entry_safe(region_t, r, _r, list) {
		list_del(&r->_l);
		region_free(r);
	}

	tlm_free(a->agent->tlm, list);
}

#define min(x, y) (x < y ? x : y)

struct list_head * region_select_random(distrm_agent_t *a, int size, int num) {
	struct list_head *list = tlm_malloc(a->agent->tlm, sizeof(struct list_head));
	INIT_LIST_HEAD(list);

	for (int i = 0; i < min(num, a->agent->dcop->hardware->number_of_resources); i++) {
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

region_t * region_get_most_distant(distrm_agent_t *a, struct list_head *list) {
	region_t *region = NULL;

	for_each_entry(region_t, r, list) {
		if (!region || region_distance(region, a) < region_distance(r, a)) {
			region = r;
		}
	}

	return region;
}

