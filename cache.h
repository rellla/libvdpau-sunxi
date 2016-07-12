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
void item_ref(int item_handle, CACHE *cache);
void item_unref(int item_handle, CACHE *cache, void(*cleanup)(void *));
int slot_get(CACHE *cache, void *item_p);

int rgba_get(CACHE *cache, void *rgba_p);
int get_visible(CACHE *cache, void *rgba_p);
int get_unvisible(CACHE *cache, void *rgba_p);

#endif
