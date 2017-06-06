#ifndef HARDWARE_H_
#define HARDWARE_H_

#include "dcop.h"
#include "list.h"
#include "view.h"

typedef struct hardware {
	int number_of_tiles;
	int number_of_resources;
	view_t *view;
} hardware_t;

#define hardware_new() (hardware_t *) calloc(1, sizeof(hardware_t))

void hardware_free(hardware_t *hw);

void hardware_load(dcop_t *dcop, hardware_t *hw);

#endif /* HARDWARE_H_ */
