#include "layout.h"
#include "pool.h"
#include "utils.h"
#include "offset_mapping.h"
#include "offset_mapping_wrapper.h"
#include <stdio.h>

void *
pmemfile_direct(PMEMfilepool *pfp, PMEMoid oid)
{
	if (oid.off == 0)
		return NULL;

	/* for tests - discard pfp->pop */
	return (void *)(oid.off);
}

struct pmemfile_block_desc *
create_block(uint64_t offset, uint32_t size,
			 struct pmemfile_block_desc *prev)
{
	struct pmemfile_block_desc *desc = calloc(1, sizeof(*desc));

	desc->offset = offset;
	desc->size = size;
	desc->prev.oid.off = (uintptr_t)prev;

	return desc;
}

struct offset_map *
offset_map_new_wrapper(PMEMfilepool *pfp)
{
	return offset_map_new(pfp);
}

void
offset_map_delete_wrapper(struct offset_map *m)
{
	return offset_map_delete(m);
}

struct pmemfile_block_desc *
block_find_closest_wrapper(struct offset_map *map, uint64_t offset)
{
	return block_find_closest(map, offset);
}

int
insert_block_wrapper(struct offset_map *map, struct pmemfile_block_desc *block)
{
	return insert_block(map, block);
}

int
remove_block_wrapper(struct offset_map *map, struct pmemfile_block_desc *block)
{
	return remove_block(map, block);
}
