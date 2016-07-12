/*
 * Copyright (c) 2016 Andreas Baierl <ichgeh@imkreisrum.de>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cache.h"
#include "rgba.h"

#define INITIAL_SIZE 1

CACHE *cache_create(void)
{
	printf("Create cache\n");
	CACHE *cache = calloc(1, sizeof(CACHE));
	cache->size = 0;
	return cache;
}

void cache_free(CACHE *cache)
{
	printf("Freeing cache\n");
	free(cache);
}

void cache_list(CACHE *cache, void(*print_cb)(void *))
{
	int index;
	printf("\n");
	for (index = 0; index < cache->size; index++)
		if (cache->data[index] == NULL)
			printf(">>> [%d @ %x] NULL\n", index, cache->data[index]);
		else
		{
			printf(">>> [%d @ %x] ", index, cache->data[index]);
			print_cb(cache->data[index]->itemdata);
			printf(", ItemData %x, Ref %d\n", cache->data[index]->itemdata, cache->data[index]->refcount);
		}
	printf("\n");

}

void rgba_ref(int rgba_handle, CACHE *cache)
{
	cache->data[rgba_handle - 1]->refcount++;
	printf("Reference rgba_handle %d in slot %d (%d)\n", rgba_handle, rgba_handle -1, cache->data[rgba_handle - 1]->refcount);
}

void rgba_unref(int rgba_handle, CACHE *cache, void(*cleanup)(void *))
{
	cache->data[rgba_handle - 1]->refcount--;
	printf("Unreference rgba_handle %d in slot %d (%d)\n", rgba_handle, rgba_handle - 1, cache->data[rgba_handle - 1]->refcount);

	if (cache->data[rgba_handle - 1]->refcount == 0)
	{
		cleanup(cache->data[rgba_handle - 1]);
		free(cache->data[rgba_handle - 1]);
		cache->data[rgba_handle - 1] = NULL;
		printf("Deleted rgba_handle\n");
	}
}

void rgba_vis(int rgba_handle, CACHE *cache)
{
	if (cache->data)
	{
		cache->data[rgba_handle - 1]->refcount = 2;
//		printf("rgba_handle %d in slot %d (%d) gets visible\n", rgba_handle, rgba_handle -1, cache->data[rgba_handle - 1]->refcount);
		((rgba_surface_t *)cache->data[rgba_handle - 1]->itemdata)->flags |= RGBA_FLAG_VISIBLE;
	}
}

void rgba_unvis(int rgba_handle, CACHE *cache, void(*cleanup)(void *))
{
	if (cache->data)
	{
		cache->data[rgba_handle - 1]->refcount = 1;
//		printf("rgba_handle %d in slot %d (%d) gets unvisible\n", rgba_handle, rgba_handle - 1, cache->data[rgba_handle - 1]->refcount);
		((rgba_surface_t *)cache->data[rgba_handle - 1]->itemdata)->flags &= ~RGBA_FLAG_VISIBLE;

		if (cache->data[rgba_handle - 1]->refcount == 0)
		{
			cleanup(cache->data[rgba_handle - 1]);
			free(cache->data[rgba_handle - 1]);
			cache->data[rgba_handle - 1] = NULL;
			printf("rgba Deleted rgba_handle\n");
		}
	}
}

int rgba_get(CACHE *cache, void *rgba)
{
	int index;
	item *data = calloc(1, sizeof(rgba));
	if (data == NULL)
		return -1;

	for (index = 0; index < cache->size; index++)
		if ((cache->data[index] == NULL) || (cache->data[index]->refcount < 2))
		{
			printf("Found rgba (%x) slot on index %d\n", rgba, index);
			break;
		}

	if (index >= cache->size)
	{
		int new_size = cache->size ? cache->size * 2 : INITIAL_SIZE;
		item **new_data = realloc(cache->data, new_size * (sizeof(item)));
		if (!new_data)
			return -1;

		memset(new_data + cache->size, 0, (new_size - cache->size) * (sizeof(item)));
		cache->data = new_data;
		cache->size = new_size;
		printf("rgba Cache resized to %d\n", cache->size);
	}

	data->refcount = 1;
	data->itemdata = rgba;

	cache->data[index] = data;

	return index + 1;
}

void *get_visible(CACHE *cache)
{
	int index;
	printf("Search for visible surface...\n");
	for (index = 0; index < cache->size; index++)
		if ((((rgba_surface_t *)cache->data[index]->itemdata)->flags & RGBA_FLAG_VISIBLE) != 0)
		{
			printf("Found visible surface on index %d\n", index);
			return (void *)cache->data[index]->itemdata;
		}

	return NULL;
}

int slot_get(CACHE *cache, void *rgba)
{
	int index;
	item *data = calloc(1, sizeof(rgba));
	if (data == NULL)
		return -1;

	printf("Get free slot for %x\n", rgba);
	for (index = 0; index < cache->size; index++)
		if (cache->data[index] == NULL)
			break;

	if (index >= cache->size)
	{
		int new_size = cache->size ? cache->size * 2 : INITIAL_SIZE;
		item **new_data = realloc(cache->data, new_size * (sizeof(item)));
		if (!new_data)
			return -1;

		memset(new_data + cache->size, 0, (new_size - cache->size) * (sizeof(item)));
		cache->data = new_data;
		cache->size = new_size;
		printf("Cache resized to %d\n", cache->size);
	}

	data->refcount++;
	data->itemdata = rgba;
	printf("Found slot for %x -> %d\n", rgba,  index);

	cache->data[index] = data;

	return index + 1;
}
