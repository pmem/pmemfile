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

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "alloc.h"
#include "hash_map.h"
#include "internal.h"
#include "os_thread.h"
#include "out.h"

#define INITIAL_NBUCKETS 2
#define HASH_P_COEFF 32212254719ULL
#define BUCKET_SIZE 2

/* hash map bucket */
struct hash_map_bucket {
	struct {
		uint64_t key;
		void *value;
	} arr[BUCKET_SIZE];
};

/* hash map */
struct hash_map {
	/* hash function coefficients */
	uint32_t hash_fun_a;
	uint32_t hash_fun_b;
	uint64_t hash_fun_p;
	unsigned short seed[3];

	/* number of elements in "buckets" */
	size_t nbuckets;

	/* buckets */
	struct hash_map_bucket *buckets;

	/* number of used slots */
	size_t entries;
};

/*
 * hash_map_rand_params -- randomizes coefficients of the hashing function
 */
static void
hash_map_rand_params(struct hash_map *map)
{
	do {
		map->hash_fun_a = (uint32_t)nrand48(map->seed);
	} while (map->hash_fun_a == 0);
	map->hash_fun_b = (uint32_t)nrand48(map->seed);
}

/*
 * hash_map_alloc -- allocates hash map
 */
struct hash_map *
hash_map_alloc(void)
{
	struct hash_map *map = pf_calloc(1, sizeof(*map));
	if (!map)
		return NULL;

	map->nbuckets = INITIAL_NBUCKETS;
	map->buckets = pf_calloc(map->nbuckets, sizeof(map->buckets[0]));
	if (!map->buckets) {
		pf_free(map);
		return NULL;
	}

	hash_map_rand_params(map);
	map->hash_fun_p = HASH_P_COEFF;

	return map;
}

/*
 * hash_map_traverse -- returns number of live entries
 */
int
hash_map_traverse(struct hash_map *map, hash_map_cb fun)
{
	int num = 0;

	for (unsigned i = 0; i < map->nbuckets; ++i) {
		struct hash_map_bucket *bucket = &map->buckets[i];

		for (unsigned j = 0; j < BUCKET_SIZE; ++j) {
			void *value = bucket->arr[j].value;
			if (value) {
				fun(bucket->arr[j].key, value);
				num++;
			}
		}
	}

	return num;
}

/*
 * hash_map_free -- destroys inode hash map
 */
void
hash_map_free(struct hash_map *map)
{
	pf_free(map->buckets);
	pf_free(map);
}

/*
 * pf_hash -- returns hash value of the key
 */
static inline size_t
pf_hash(struct hash_map *map, uint64_t key)
{
	return (map->hash_fun_a * key + map->hash_fun_b) % map->hash_fun_p;
}

/*
 * hash_map_rebuild -- rebuilds the whole inode hash map
 *
 * Returns 0 on success, negative value (-errno) on failure, 1 on hash map
 * conflict.
 */
static int
hash_map_rebuild(struct hash_map *c, size_t new_sz)
{
	struct hash_map_bucket *new_buckets =
			pf_calloc(new_sz, sizeof(new_buckets[0]));
	size_t idx;

	if (!new_buckets)
		return -errno;

	for (size_t i = 0; i < c->nbuckets; ++i) {
		struct hash_map_bucket *b = &c->buckets[i];

		for (unsigned j = 0; j < BUCKET_SIZE; ++j) {
			if (b->arr[j].key == 0)
				continue;

			idx = pf_hash(c, b->arr[j].key) % new_sz;
			struct hash_map_bucket *newbucket = &new_buckets[idx];
			unsigned k;
			for (k = 0; k < BUCKET_SIZE; ++k) {
				if (newbucket->arr[k].key == 0) {
					newbucket->arr[k] = b->arr[j];
					break;
				}
			}

			if (k == BUCKET_SIZE) {
				pf_free(new_buckets);
				return 1;
			}
		}
	}

	pf_free(c->buckets);
	c->nbuckets = new_sz;
	c->buckets = new_buckets;

	return 0;
}

/*
 * hash_map_remove -- removes key/value from the hash map
 */
int
hash_map_remove(struct hash_map *map, uint64_t key,
		void *value)
{
	size_t idx = pf_hash(map, key) % map->nbuckets;
	struct hash_map_bucket *b = &map->buckets[idx];
	unsigned j;
	for (j = 0; j < BUCKET_SIZE; ++j) {
		if (b->arr[j].value == value) {
			memset(&b->arr[j], 0, sizeof(b->arr[j]));
			break;
		}
	}

	if (j == BUCKET_SIZE)
		return -ENOENT;

	map->entries--;

	return 0;
}

/*
 * hash_map_get -- returns value associated with specified key
 */
void *
hash_map_get(struct hash_map *map, uint64_t key)
{
	size_t idx = pf_hash(map, key) % map->nbuckets;

	struct hash_map_bucket *b = &map->buckets[idx];
	for (unsigned j = 0; j < BUCKET_SIZE; ++j) {
		if (b->arr[j].key == key)
			return b->arr[j].value;
	}

	return NULL;
}

/*
 * hash_map_put -- inserts key/value into hash map
 *
 * Returns existing value if key already existed or inserted value.
 */
void *
hash_map_put(struct hash_map *map, uint64_t key, void *value)
{
	size_t idx = pf_hash(map, key) % map->nbuckets;

	struct hash_map_bucket *b = &map->buckets[idx];
	unsigned empty_slot = UINT32_MAX;
	for (unsigned j = 0; j < BUCKET_SIZE; ++j) {
		if (b->arr[j].key == key)
			return b->arr[j].value;

		if (empty_slot == UINT32_MAX && b->arr[j].key == 0)
			empty_slot = j;
	}

	int tries = 0;
	while (empty_slot == UINT32_MAX) {
		size_t new_sz = map->nbuckets;

		int res;
		do {
			if (map->entries > 2 * new_sz || tries == 2) {
				new_sz *= 2;
				tries = 0;
			} else {
				hash_map_rand_params(map);
				tries++;
			}
		} while ((res = hash_map_rebuild(map, new_sz)) == 1);

		if (res < 0) {
			errno = -res;
			return NULL;
		}

		idx = pf_hash(map, key) % map->nbuckets;
		b = &map->buckets[idx];

		for (unsigned j = 0; j < BUCKET_SIZE; ++j) {
			if (b->arr[j].key == 0) {
				empty_slot = j;
				break;
			}
		}
	}

	b->arr[empty_slot].key = key;
	b->arr[empty_slot].value = value;
	map->entries++;

	return value;
}
