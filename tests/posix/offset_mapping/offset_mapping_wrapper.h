#ifndef OFFSET_MAPPING_WRAPPER_H
#define OFFSET_MAPPING_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "libpmemfile-posix.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pmemfile_block_desc;
struct offset_map;

struct pmemfile_block_desc *create_block(uint64_t offset, uint32_t size,
	struct pmemfile_block_desc *prev);

struct offset_map *offset_map_new_wrapper(PMEMfilepool *pfp);

void offset_map_delete_wrapper(struct offset_map *m);

struct pmemfile_block_desc *block_find_closest_wrapper(struct offset_map *map,
	uint64_t offset);

int insert_block_wrapper(struct offset_map *map,
	struct pmemfile_block_desc *block);

int remove_block_wrapper(struct offset_map *map,
	struct pmemfile_block_desc *block);

#ifdef __cplusplus
}
#endif
#endif
