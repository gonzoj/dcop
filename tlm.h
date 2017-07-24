#ifndef TLM_H_
#define TLM_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "list.h"

typedef struct tlm_entry {
	struct list_head _l;
	void *base;
	size_t size;
	bool free;
} tlm_entry_t;

typedef struct tlm {
	void *base;
	size_t size;
	tlm_entry_t *buf;
	struct list_head entries;
	pthread_mutex_t m;
	size_t cur_used;
	size_t max_used;
} tlm_t;

tlm_t * tlm_create(size_t kbytes);

void * tlm_malloc(tlm_t *tlm, size_t size);

void tlm_free(tlm_t *tlm, void *p);

void tlm_destroy(tlm_t *tlm);

void tlm_touch(tlm_t *tlm);

char * tlm_strdup(tlm_t *tlm, const char *s);

void * tlm_realloc(tlm_t *tlm, void *p, size_t size);

#endif /* TLM_H_ */

