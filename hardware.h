#ifndef HARDWARE_H_
#define HARDWARE_H_

#include "dcop.h"
#include "list.h"

typedef struct hardware {
	int number_of_tiles;
	int number_of_resources;
	struct list_head resources;
} hardware_t;

hardware_t * hardware_new();

void hardware_free(hardware_t *hw);

void hardware_load(dcop_t *dcop, hardware_t *hw);

#endif /* HARDWARE_H_ */

