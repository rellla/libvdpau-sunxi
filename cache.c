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

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cache.h"

#define INITIAL_SIZE 2

CACHE *cache_create(void)
{
	CACHE *cache = calloc(1, sizeof(CACHE));
	cache->size = 0;
	cache->head = -1;
	pthread_mutex_init(&cache->mutex, NULL);
	return cache;
}

void cache_free(CACHE *cache, void(*cleanup)(void *))
{
	int i;

	if (!cache)
		return;

	pthread_mutex_lock(&cache->mutex);
	for (i = 0; i < cache->size; i++)
	{
		if (cache->data[i])
		{
			if (cache->data[i]->itemdata)
			{
				cleanup(cache->data[i]->itemdata);
				cache->data[i]->itemdata = NULL;
			}
			free(cache->data[i]);
			cache->data[i] = NULL;
		}
	}
	pthread_mutex_unlock(&cache->mutex);
	pthread_mutex_destroy(&cache->mutex);

	free(cache);
}

void cache_set_head(CACHE *cache, int item_handle)
{
	pthread_mutex_lock(&cache->mutex);
	cache->head = item_handle - 1;
	pthread_mutex_unlock(&cache->mutex);
}

int cache_get_head(CACHE *cache, void **item_p)
{
	int head;

	pthread_mutex_lock(&cache->mutex);
	if (cache->head < 0)
	{
		pthread_mutex_unlock(&cache->mutex);
		return 0;
	}

	*item_p = cache->data[cache->head]->itemdata;

	head = cache->head + 1;
	pthread_mutex_unlock(&cache->mutex);

	return head;
}

void cache_get_pointer(int item_handle, CACHE *cache, void **item_p)
{
	if (!item_handle || !cache)
		return;

	pthread_mutex_lock(&cache->mutex);
	if (cache->data[item_handle - 1])
		*item_p = cache->data[item_handle - 1]->itemdata;
	pthread_mutex_unlock(&cache->mutex);
}

int cache_hdl_create(CACHE *cache, void *item_p)
{
	int index;
	item *data = calloc(1, sizeof(item_p));
	if (data == NULL)
		return -1;

	pthread_mutex_lock(&cache->mutex);

	for (index = 0; index < cache->size; index++)
		if (cache->data[index] == NULL)
			break;

	if ((cache->size == 0) && (index == 0))
		cache->head = 0;

	if (index >= cache->size)
	{
		int new_size = cache->size ? cache->size * 2 : INITIAL_SIZE;
		item **new_data = realloc(cache->data, new_size * (sizeof(item)));
		if (!new_data)
			return -1;

		memset(new_data + cache->size, 0, (new_size - cache->size) * (sizeof(item)));
		cache->data = new_data;
		cache->size = new_size;
	}

	data->refcount = 0;
	data->itemdata = item_p;
	cache->data[index] = data;

	pthread_mutex_unlock(&cache->mutex);

	return index + 1;
}

void cache_hdl_unref(int item_handle, CACHE *cache, void(*cleanup)(void *))
{
	if (!item_handle || !cache)
		return;

	pthread_mutex_lock(&cache->mutex);
	if (cache->data[item_handle - 1] == NULL)
	{
		pthread_mutex_unlock(&cache->mutex);
		return;
	}

	cache->data[item_handle - 1]->refcount--;

	if (cache->data[item_handle - 1]->refcount < 0)
	{
		if (cache->data[item_handle - 1]->itemdata)
		{
			cleanup(cache->data[item_handle - 1]->itemdata);
			cache->data[item_handle - 1]->itemdata = NULL;
		}
		free(cache->data[item_handle - 1]);
		cache->data[item_handle - 1] = NULL;
	}

	pthread_mutex_unlock(&cache->mutex);
}

void cache_hdl_ref(int item_handle, CACHE *cache)
{
	if (!item_handle || !cache)
		return;

	pthread_mutex_lock(&cache->mutex);
	if (cache->data[item_handle - 1] == NULL)
	{
		pthread_mutex_unlock(&cache->mutex);
		return;
	}

	cache->data[item_handle - 1]->refcount++;

	pthread_mutex_unlock(&cache->mutex);
}

int cache_hdl_get_ref(int item_handle, CACHE *cache)
{
	if (!item_handle || !cache)
		return -1;

	int refcount;

	pthread_mutex_lock(&cache->mutex);
	if (cache->data[item_handle - 1] == NULL)
	{
		pthread_mutex_unlock(&cache->mutex);
		return -1;
	}

	refcount = cache->data[item_handle - 1]->refcount;
	pthread_mutex_unlock(&cache->mutex);

	return refcount;
}

int cache_hdl_get(CACHE *cache, void **item_p)
{
	int index;

	pthread_mutex_lock(&cache->mutex);
	for (index = 0; index < cache->size; index++)
		if (cache->data[index] && (cache->data[index]->refcount == 0))
		{
			*item_p = cache->data[index]->itemdata;
			pthread_mutex_unlock(&cache->mutex);
			return index + 1;
		}

	pthread_mutex_unlock(&cache->mutex);
	return 0;
}

void cache_list(CACHE *cache, void(*print_cb)(void *))
{
	int index;
	pthread_mutex_lock(&cache->mutex);
	printf("\n");
	for (index = 0; index < cache->size; index++)
		if (cache->data[index] == NULL)
			printf(">>> [%d @ %x] NULL\n", index, (unsigned int)cache->data[index]);
		else
		{
			printf(">>> [%d @ %x] ", index, (unsigned int)cache->data[index]);
			print_cb(cache->data[index]->itemdata);
			printf(", ItemData %x, Ref %d\n", (unsigned int)cache->data[index]->itemdata, cache->data[index]->refcount);
		}
	printf("\n");
	pthread_mutex_unlock(&cache->mutex);
}
