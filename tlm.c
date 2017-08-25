#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "console.h"
#include "list.h"
#include "tlm.h"

//#define MAX_ENTRIES 8192
#define MAX_ENTRIES 32768

tlm_t * tlm_create(size_t kbytes) {
	tlm_t *tlm = malloc(sizeof(tlm_t));
	tlm->size = kbytes * 1024;
	posix_memalign(&tlm->base, sysconf(_SC_PAGE_SIZE), tlm->size + sizeof(tlm_entry_t) * MAX_ENTRIES);
	memset(tlm->base, 0, tlm->size + sizeof(tlm_entry_t) * MAX_ENTRIES);
	tlm->buf = tlm->base;

	INIT_LIST_HEAD(&tlm->entries);

	tlm_entry_t *entry = &tlm->buf[0];
	entry->base = (char *) tlm->base + sizeof(tlm_entry_t) * MAX_ENTRIES;
	entry->size = tlm->size;
	entry->free = true;
	list_add_tail(&entry->_l, &tlm->entries);

	pthread_mutex_init(&tlm->m, NULL);

	tlm->cur_used = 0;
	tlm->max_used = 0;

	return tlm;
}

void * tlm_malloc(tlm_t *tlm, size_t size) {
	if (!tlm) {
		return calloc(1, size);
	}

	if (size % 8) {
		size = ((size / 8) + 1) * 8;
	}

	if (tlm->size < size) {
		print_error("tlm: failed to allocate memory (%i bytes)\n", size);

		return NULL;
	}

	pthread_mutex_lock(&tlm->m);

	for_each_entry(tlm_entry_t, e, &tlm->entries) {
		if (e->free && e->size >= size) {
			tlm_entry_t *entry = NULL;
			for (int i = 0; i < MAX_ENTRIES; i++) {
				if (tlm->buf[i].base == NULL) {
					entry = &tlm->buf[i];
					break;
				}
			}
			if (!entry) {
				pthread_mutex_unlock(&tlm->m);

				print_error("tlm: no free TLM entries for accounting left\n");
				print_error("tlm: failed to allocate memory (%i bytes)\n", size);

				return NULL;
			}

			entry->base = e->base;
			entry->size = size;
			entry->free = false;
			list_add_tail(&entry->_l, &tlm->entries);

			e->base = (char *) e->base + size;
			e->size -= size;

			if (e->size == 0) {
				e->base = NULL;
				list_del(&e->_l);
			}

			tlm->cur_used += size;
			if (tlm->cur_used > tlm->max_used) {
				tlm->max_used = tlm->cur_used;
			}

			pthread_mutex_unlock(&tlm->m);

			memset(entry->base, 0, size);

			return entry->base;
		}
	}

	pthread_mutex_unlock(&tlm->m);

	print_error("tlm: failed to allocate memory (%i bytes)\n", size);
	print_error("tlm: memory currently used: %u KB\n", tlm->cur_used / 1024);

	return NULL;
}

static tlm_entry_t * tlm_prev(tlm_t *tlm, tlm_entry_t *e) {
	for_each_entry(tlm_entry_t, _e, &tlm->entries) {
		if ((char *) _e->base + _e->size == (char *) e->base) {
			return _e;
		}
	}

	return NULL;
}

static tlm_entry_t * tlm_next(tlm_t *tlm, tlm_entry_t *e) {
	for_each_entry(tlm_entry_t, _e, &tlm->entries) {
		if ((char *) _e->base == (char *) e->base + e->size) {
			return _e;
		}
	}

	return NULL;
}

void tlm_free(tlm_t *tlm, void *p) {
	if (!tlm) {
		free(p);

		return;
	}

	pthread_mutex_lock(&tlm->m);

	for_each_entry(tlm_entry_t, e, &tlm->entries) {
		if (!e->free && e->base == p) {
			tlm->cur_used -= e->size;

			e->free = true;

			tlm_entry_t *prev = tlm_prev(tlm, e);
			tlm_entry_t *next = tlm_next(tlm, e);

			if (prev && prev->free) {
				e->base = prev->base;
				e->size += prev->size;

				prev->base = NULL;
				list_del(&prev->_l);
			}

			if (next && next->free) {
				e->size += next->size;

				next->base = NULL;
				list_del(&next->_l);
			}

			pthread_mutex_unlock(&tlm->m);

			return;
		}
	}

	print_warning("tlm (%lX): failed to free pointer %lX\n", tlm->base, p);

	pthread_mutex_unlock(&tlm->m);
}

void tlm_destroy(tlm_t *tlm) {
	if (tlm) {
		free(tlm->base);
		pthread_mutex_destroy(&tlm->m);
		free(tlm);
	}
}

void __attribute__((optimize("O0"))) tlm_touch(tlm_t *tlm) {
	if (!tlm) {
		return;
	}

	for (int i = 0; i < tlm->size + sizeof(tlm_entry_t) * MAX_ENTRIES; i++) {
		char c = ((char *) tlm->base)[i];
		((volatile char *) tlm->base)[i] = c;
	}
}

char * tlm_strdup(tlm_t *tlm, const char *s) {
	size_t len = strlen(s);
	char *_s = (char *) tlm_malloc(tlm, len + 1);
	memcpy(_s, s, len);

	return _s;
}

void * tlm_realloc(tlm_t *tlm, void *p, size_t size) {
	if (!tlm) {
		return realloc(p, size);
	}

	if (!p) {
		return tlm_malloc(tlm, size);
	}

	if (p && size == 0) {
		tlm_free(tlm, p);

		return NULL;
	}

	pthread_mutex_lock(&tlm->m);

	for_each_entry(tlm_entry_t, e, &tlm->entries) {
		if (e->base == p) {
			if (e->size == size) {
				pthread_mutex_unlock(&tlm->m);

				return e->base;
			} else if (e->size > size) {
				size_t diff = e->size - size;
				e->size -= size;

				tlm_entry_t *entry = NULL;
				for (int i = 0; i < MAX_ENTRIES; i++) {
					if (tlm->buf[i].base == NULL) {
						entry = &tlm->buf[i];
						break;
					}
				}
				if (!entry) {
					pthread_mutex_unlock(&tlm->m);

					print_error("tlm: failed to re-allocate memory (%i bytes)\n", size);

					return NULL;
				}


				entry->base = (char *) e->base + e->size;
				entry->size = diff;
				entry->free = true;
				list_add_tail(&entry->_l, &tlm->entries);

				tlm->cur_used -= diff;

				pthread_mutex_unlock(&tlm->m);

				return e->base;
			} else {
				size_t diff = size - e->size;

				tlm->cur_used += diff;
				if (tlm->cur_used > tlm->max_used) {
					tlm->max_used = tlm->cur_used;
				}

				tlm_entry_t *prev = tlm_prev(tlm, e);
				tlm_entry_t *next = tlm_next(tlm, e);


				if (prev && prev->free && prev->size >= diff) {
					e->size += diff;
					e->base = (char *) e->base - diff;

					memmove(e->base, (char *) e->base + diff, e->size - diff);

					prev->size -= diff;

					if (prev->size == 0) {
						prev->base = NULL;
						list_del(&prev->_l);
					}

					pthread_mutex_unlock(&tlm->m);

					return e->base;
				} else if (next && next->free && next->size >= diff) {
					e->size += diff;

					next->base = (char *) next->base + diff;
					next->size -= diff;

					if (next->size == 0) {
						next->base = NULL;
						list_del(&next->_l);
					}

					pthread_mutex_unlock(&tlm->m);

					return e->base;
				} else {
					size_t rem = e->size;

					pthread_mutex_unlock(&tlm->m);

					void *_p = tlm_malloc(tlm, size);

					memcpy(_p, p, rem);

					tlm_free(tlm, p);

					return _p;
				}
			}
		}
	}

	pthread_mutex_unlock(&tlm->m);

	print_error("tlm (%lX): failed to re-allocate pointer %lX\n", tlm->base, p);

	return NULL;
}

