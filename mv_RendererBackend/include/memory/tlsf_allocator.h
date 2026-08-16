#ifndef _TLSF_ALLOCATOR_H_
#define _TLSF_ALLOCATOR_H_

#include <vector>

#include "util/types.h"

namespace mv
{
	namespace memory
	{
		using namespace types;

		constexpr u64 MIN_ALIGNMENT = 8;

		constexpr u64 FL_BITS = 5;
		constexpr u64 SL_BITS = 4;

		constexpr u64 FL_SIZE = (1 << FL_BITS);
		constexpr u64 SL_SIZE = (1 << SL_BITS);

		static inline u64 alignUp(u64 v, u64 a)
		{
			return (v + a - 1) & ~(a - 1);
		}

		struct Block
		{
			u64 offset;
			u64 size;

			Block* prev_phys;
			Block* next_phys;

			Block* prev_free;
			Block* next_free;

			bool free;
		};

		class BlockPool
		{
		public:
			BlockPool()
				: head(nullptr)
			{

			}

			~BlockPool()
			{
				for (void* c : chunks) ::operator delete(c);
			}

			Block* allocate()
			{
				if (!head)
					grow();
				Block* block = head;
				head = head->next_free;
				return block;
			}

			void free(Block* block)
			{
				block->next_free = head;
				head = block;
			}

		private:
			void grow()
			{
				const u64 size = 512;
				Block* chunk = static_cast<Block*>(::operator new(sizeof(Block) * size));
				chunks.push_back(chunk);

				for (u64 i = 0; i < size; ++i)
				{
					Block* block = &chunk[i];
					block->next_free = head;
					head = block;
				}
			}

		private:
			std::vector<void*> chunks;
			Block* head;
		};

		class TLSF
		{
		public:
			TLSF();
			~TLSF();

			void initialize(u64 size);

			Block* allocate(u64 size, u64 alignment);

			void free(Block* block);


		private:
			void insert(Block* b);

			void remove(Block* b);

			Block* merge(Block* b);


			void mapping(u64 size, u64& fl, u64& sl);

			void split(Block* b, u64 size);

		public:
			void print_info();

		private:
			BlockPool pool;

			u64 fl_bitmap;
			u64 sl_bitmap[FL_SIZE];

			Block* header;

			Block* free_list[FL_SIZE][SL_SIZE];
		};
	}
}

#endif
