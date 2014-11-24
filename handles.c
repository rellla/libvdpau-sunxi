/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
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
#include "vdpau_private.h"
#include <pthread.h>

#define INITIAL_SIZE 16

static struct
{
	void **data;
	int size;
	pthread_mutex_t mutex;
} ht;

pthread_mutex_t handle_table_mutex = PTHREAD_MUTEX_INITIALIZER;

int handle_create(void *data)
{
	int index;

	if (!data)
		return -1;
		
	pthread_mutex_lock(&handle_table_mutex);

	for (index = 0; index < ht.size; index++)
		if (ht.data[index] == NULL)
			break;

	if (index >= ht.size)
	{
		int new_size = ht.size ? ht.size * 2 : INITIAL_SIZE;
		void **new_data = realloc(ht.data, new_size * sizeof(void *));
		if (!new_data) {
			pthread_mutex_unlock(&handle_table_mutex);
			return -1;
		}

		memset(new_data + ht.size, 0, (new_size - ht.size) * sizeof(void *));
		ht.data = new_data;
		ht.size = new_size;
	}

	ht.data[index] = data;
	pthread_mutex_init(&ht.mutex, NULL);
	
	pthread_mutex_unlock(&handle_table_mutex);
	
	return index + 1;
}

void *handle_get(int handle)
{
	if (handle == VDP_INVALID_HANDLE)
		return NULL;

	int index = handle - 1;
	
	pthread_mutex_lock(&handle_table_mutex);
	
	if (index < ht.size) {
		void *ret = ht.data[index];
		pthread_mutex_unlock(&handle_table_mutex);
		return ret;
	}
	
	pthread_mutex_unlock(&handle_table_mutex);

	return NULL;
}

void handle_destroy(int handle)
{
	int index = handle - 1;

	pthread_mutex_lock(&handle_table_mutex);
	
	if (index < ht.size)
		ht.data[index] = NULL;
		
	pthread_mutex_unlock(&handle_table_mutex);
}

void handle_acquire(int handle) {
	if (handle == VDP_INVALID_HANDLE)
		return;

	int index = handle - 1;
	if (index < ht.size)
		pthread_mutex_lock(&ht.mutex);
}

void handle_release(int handle) {
	if (handle == VDP_INVALID_HANDLE)
		return;

	int index = handle - 1;
	if (index < ht.size)
		pthread_mutex_unlock(&ht.mutex);
}
