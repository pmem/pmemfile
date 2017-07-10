/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pthread.h>

#include "cache.h"
#include "inode.h"

static struct cache_entry {
	size_t value;
	void *tag;
	pthread_spinlock_t lock;
} cache[CACHE_SIZE];

static struct cache_entry *
cache_entry(void *key)
{
	return &cache[((intptr_t) key) % CACHE_SIZE];
}

void
cache_init()
{
	for (int i = 0; i < CACHE_SIZE; i++)
		pthread_spin_init(&cache[i].lock, PTHREAD_PROCESS_SHARED);
}

bool
is_cache_valid(size_t cache_value)
{
	return cache_value != CACHE_INVALID;
}

void
cache_set(void *key, size_t value)
{
	pthread_spin_lock(&cache_entry(key)->lock);

	cache_entry(key)->value = value;
	cache_entry(key)->tag = key;

	pthread_spin_unlock(&cache_entry(key)->lock);
}

size_t
cache_get(void *key)
{
	size_t ret;

	pthread_spin_lock(&cache_entry(key)->lock);

	if (cache_entry(key)->tag == key)
		ret = cache_entry(key)->value;
	else
		ret = CACHE_INVALID;

	pthread_spin_unlock(&cache_entry(key)->lock);

	return ret;
}
