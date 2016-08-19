/*
 * Copyright (c) 2016-2017 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __CACHE_H__
#define __CACHE_H__

#include <pthread.h>

typedef struct{
	unsigned int refcount;
	void *itemdata;
} item;

typedef struct
{
	item **data;
	int size;
	int head;
	pthread_mutex_t mutex;
} CACHE;

CACHE *cache_create(void);
void cache_free(CACHE *cache, void(*cleanup)(void *));
void cache_list(CACHE *cache, void(*print_cb)(void *));
void cache_set_head(CACHE *cache, int item_handle);
int cache_get_head(CACHE *cache, void **item_p);

int cache_hdl_create(CACHE *cache, void *item_p);
void cache_hdl_unref(int item_handle, CACHE *cache, void(*cleanup)(void *));
void cache_hdl_ref(int item_handle, CACHE *cache);
int cache_hdl_get_ref(int item_handle, CACHE *cache);
int cache_hdl_get(CACHE *cache, void **item_p);
void cache_get_pointer(int item_handle, CACHE *cache, void **item_p);
#endif
