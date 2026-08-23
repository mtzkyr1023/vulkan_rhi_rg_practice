
#include <bit>
#include <iostream>
#include <bitset>

#include "memory/tlsf_allocator.h"

namespace mv::memory
{
	TLSF::TLSF()
		: fl_bitmap(0)
		, header(nullptr)
	{
		memset(sl_bitmap, 0, sizeof(sl_bitmap));
		memset(free_list, 0, sizeof(free_list));
	}

	TLSF::~TLSF()
	{

	}

	void TLSF::initialize(u64 size)
	{
		fl_bitmap = 0;
		memset(free_list, 0, sizeof(free_list));
		memset(sl_bitmap, 0, sizeof(sl_bitmap));
		header = pool.allocate();

		header->offset = 0;
		header->size = size;
		header->prev_phys = nullptr;
		header->next_phys = nullptr;
		header->prev_free = nullptr;
		header->next_free = nullptr;
		header->free = true;

		insert(header);
	}

	u64 TLSF::requiredPoolSize(u64 size, u64 alignment)
	{
		if (alignment < MIN_ALIGNMENT)
			alignment = MIN_ALIGNMENT;

		// A free block's offset carries no alignment guarantee, so the search has to allow
		// for the padding that will be trimmed off its front: at most alignment - 1 bytes.
		u64 search_size = alignUp(size, MIN_ALIGNMENT) + alignment - 1;

		// mapping() rounds down, so a bucket's blocks can be smaller than what was asked
		// for. Rounding the request up to the bucket boundary first makes every block in
		// the selected bucket a valid candidate.
		if (search_size >= SL_SIZE)
		{
			const u64 f = 63 - std::countl_zero(search_size);
			search_size = alignUp(search_size, 1ull << (f - SL_BITS));
		}

		return search_size;
	}

	Block* TLSF::allocate(u64 size, u64 alignment)
	{
		if (alignment < MIN_ALIGNMENT)
			alignment = MIN_ALIGNMENT;

		const u64 aligned_size = alignUp(size, MIN_ALIGNMENT);
		const u64 search_size = requiredPoolSize(size, alignment);

		u64 fl, sl;
		mapping(search_size, fl, sl);

		u64 sl_map = sl_bitmap[fl] & (~0ULL << sl);
		if (!sl_map)
		{
			u64 fl_map = fl_bitmap & (~0ULL << (fl + 1));
			if (!fl_map)
				return nullptr;

			fl = std::countr_zero(fl_map);
			sl_map = sl_bitmap[fl];
		}
		sl = std::countr_zero(sl_map);
		Block* block = free_list[fl][sl];
		if (!block)
			return nullptr;

		remove(block);

		// Trim the misaligned head into its own free block so the payload starts aligned.
		const u64 pad = alignUp(block->offset, alignment) - block->offset;
		if (pad > 0)
		{
			split(block, pad);

			// split() leaves the tail on the free list and the head off it; we want the
			// opposite, since the tail is the part being handed out.
			Block* rest = block->next_phys;
			remove(rest);

			block->free = true;
			insert(block);

			block = rest;
		}

		if (block->size < aligned_size)
		{
			insert(block);
			return nullptr;
		}

		split(block, aligned_size);

		block->free = false;

		//std::cout << "Allocated block at offset " << block->offset << " with size " << block->size << std::endl;

		return block;
	}

	void TLSF::free(Block* block)
	{
		//std::cout << "Freed block at offset " << block->offset << " with size " << block->size << std::endl;

		block->free = true;
		Block* merged = merge(block);
		insert(merged);
	}

	void TLSF::insert(Block* b)
	{
		u64 fl, sl;
		mapping(b->size, fl, sl);

		b->next_free = free_list[fl][sl];
		// The new head has nothing before it. Leaving a stale prev_free here makes a later
		// remove() write through it and corrupt an unrelated list.
		b->prev_free = nullptr;

		if (b->next_free)
			b->next_free->prev_free = b;

		free_list[fl][sl] = b;

		fl_bitmap |= (1ull << fl);
		sl_bitmap[fl] |= (1ull << sl);
	}

	void TLSF::remove(Block* b)
	{
		u64 fl, sl;
		mapping(b->size, fl, sl);

		if (b->prev_free)
			b->prev_free->next_free = b->next_free;

		if (b->next_free)
			b->next_free->prev_free = b->prev_free;

		if (free_list[fl][sl] == b)
			free_list[fl][sl] = b->next_free;

		if (!free_list[fl][sl])
			sl_bitmap[fl] &= ~(1ull << sl);

		if (!sl_bitmap[fl])
			fl_bitmap &= ~(1ull << fl);

		b->prev_free = nullptr;
		b->next_free = nullptr;
	}

	Block* TLSF::merge(Block* b)
	{
		Block* next = b->next_phys;
		if (next && next->free)
		{
			remove(next);
			b->size += next->size;

			// The absorbed block leaves the physical list before it is recycled; skipping
			// this leaves b->next_phys dangling at a block handed back to the pool.
			b->next_phys = next->next_phys;
			if (b->next_phys)
				b->next_phys->prev_phys = b;

			pool.free(next);
		}

		Block* prev = b->prev_phys;
		if (prev && prev->free)
		{
			remove(prev);
			prev->size += b->size;

			prev->next_phys = b->next_phys;
			if (prev->next_phys)
				prev->next_phys->prev_phys = prev;

			pool.free(b);

			return prev;
		}

		return b;
	}


	void TLSF::mapping(u64 size, u64& fl, u64& sl)
	{
		if (size < SL_SIZE)
		{
			fl = 0;
			sl = size / (SL_SIZE / SL_SIZE);
		}
		else
		{
			fl = 63 - std::countl_zero(size);
			sl = (size >> (fl - SL_BITS)) & (SL_SIZE - 1);
		}
	}

	void TLSF::split(Block* b, u64 size)
	{
		if (b->size > size)
		{
			Block* new_block = pool.allocate();
			new_block->offset = b->offset + size;
			new_block->size = b->size - size;
			new_block->prev_phys = b;
			new_block->next_phys = b->next_phys;
			new_block->prev_free = nullptr;
			new_block->next_free = nullptr;
			new_block->free = true;
			b->size = size;
			b->next_phys = new_block;
			if (new_block->next_phys)
				new_block->next_phys->prev_phys = new_block;

			insert(new_block);
		}
	}

	void TLSF::print_info()
	{
		std::cout << "FL Bitmap: " << std::bitset<FL_SIZE>(fl_bitmap) << std::endl;
		for (u64 fl = 0; fl < FL_SIZE; ++fl)
		{
			if (fl_bitmap & (1ull << fl))
			{
				std::cout << "  FL " << fl << ": SL Bitmap: " << std::bitset<SL_SIZE>(sl_bitmap[fl]) << std::endl;
				for (u64 sl = 0; sl < SL_SIZE; ++sl)
				{
					if (sl_bitmap[fl] & (1ull << sl))
					{
						std::cout << "    SL " << sl << ": ";
						Block* block = free_list[fl][sl];
						while (block)
						{
							std::cout << "[" << block->offset << ", " << block->size << "] ";
							block = block->next_free;
						}
						std::cout << std::endl;
					}
				}
			}
		}
	}
}
