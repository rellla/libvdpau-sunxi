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
			printf(">>> [%d(%d) @ %x] NULL\n", index, index + 1, cache->data[index]);
		else
		{
			printf(">>> [%d @ %x] ", index, cache->data[index]);
			print_cb(cache->data[index]->itemdata);
			printf(", ItemData %x, Ref %d\n", cache->data[index]->itemdata, cache->data[index]->refcount);
		}
	printf("\n");
}

void item_ref(int item_handle, CACHE *cache)
{
	cache->data[item_handle - 1]->refcount++;
//	printf("Reference item %d in slot %d (%d)\n", item_handle, item_handle -1, cache->data[item_handle - 1]->refcount);
}

void item_unref(int item_handle, CACHE *cache, void(*cleanup)(void *))
{
//	if (cache->data[item_handle - 1] == NULL)
//		return;

//	printf("unreference item %d in slot %d : %d\n", item_handle, item_handle - 1, cache->data[item_handle - 1]->refcount);
	cache->data[item_handle - 1]->refcount--;
//	printf("Unreference item %d in slot %d (%d)\n", item_handle, item_handle - 1, cache->data[item_handle - 1]->refcount);

	if (cache->data[item_handle - 1]->refcount == 0)
	{
		cleanup(cache->data[item_handle - 1]->itemdata);
//		free(cache->data[item_handle - 1]);
//		cache->data[item_handle - 1] = NULL;
		printf("Deleted item\n");
	}
}

void rgba_ref(int item_handle, CACHE *cache)
{
	if (cache->data[item_handle - 1] == NULL)
		return;

	cache->data[item_handle - 1]->refcount++;
//	printf("Reference item %d in slot %d (%d)\n", item_handle, item_handle -1, cache->data[item_handle - 1]->refcount);
}


void rgba_unref(int item_handle, CACHE *cache)
{
	if (cache->data[item_handle - 1] == NULL)
		return;

	if (cache->data[item_handle -1]->refcount > 1)
	{
//		printf("unreference item %d in slot %d : %d\n", item_handle, item_handle - 1, cache->data[item_handle - 1]->refcount);
		cache->data[item_handle - 1]->refcount--;
	}
}

int slot_get(CACHE *cache, void *item_p)
{
	int index;
	item *data = calloc(1, sizeof(item_p));
	if (data == NULL)
		return -1;

	printf("Get free slot for %x\n", item_p);
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
	data->itemdata = item_p;
	printf("Found slot for %x -> %d\n", item_p,  index);

	cache->data[index] = data;

	return index + 1;
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

	data->refcount++;
	data->itemdata = rgba;
	printf("itemdata: %x, rgba: %x\n", data->itemdata, rgba);

	cache->data[index] = data;

	return index + 1;
}

int get_visible(CACHE *cache, void **item_p)
{
	int index;
	for (index = 0; index < cache->size; index++)
	{
		if (cache->data[index] && (cache->data[index]->refcount > 1) && (cache->data[index]->itemdata != NULL))
		{
			*item_p = cache->data[index]->itemdata;
//			printf("Found visible surface %x on index %d\n", cache->data[index]->itemdata, index);
			return index + 1;
		}
	}

	printf("No visible surface found\n");
	return 0;
}

int get_unvisible(CACHE *cache, void **item_p)
{
	int index;
	for (index = 0; index < cache->size; index++)
		if (cache->data[index] && (cache->data[index]->refcount <= 1))
		{
			if (cache->data[index]->itemdata != NULL)
				*item_p = cache->data[index]->itemdata;
			printf("Found unvisible surface %x on index %d\n", cache->data[index]->itemdata, index);
			return index + 1;
		}

	printf("No invisible surface found\n");
	return 0;
}
