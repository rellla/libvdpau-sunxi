#ifndef __CACHE_H__
#define __CACHE_H__

typedef struct{
	unsigned int refcount;
	void *itemdata;
} item;

typedef struct
{
	item **data;
	int size;
} CACHE;

CACHE *cache_create(void);
void cache_list(CACHE *cache, void(*print_cb)(void *));
void cache_free(CACHE *cache);
void rgba_ref(int rgba_handle, CACHE *cache);
void rgba_unref(int rgba_handle, CACHE *cache, void(*cleanup)(void *));
int slot_get(CACHE *cache, void *rgba_p);

#endif
