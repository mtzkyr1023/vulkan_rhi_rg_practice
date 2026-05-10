
#include "iostream"
#include "bitset"
#include "bit"
#include "vector"
#include "ctime"
#include "chrono"

#include "types.h"
using namespace mv::types;

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
	TLSF()
		: fl_bitmap(0)
		, header(nullptr)
	{
		memset(sl_bitmap, 0, sizeof(sl_bitmap));
		memset(free_list, 0, sizeof(free_list));
	}

	void initialize(u64 size)
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

	Block* allocate(u64 size, u64 alignment)
	{
		if (alignment < MIN_ALIGNMENT)
			alignment = MIN_ALIGNMENT;

		u64 aligned_size = alignUp(size, alignment);

		u64 fl, sl;
		mapping(aligned_size, fl, sl);

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

		remove(block);

		split(block, aligned_size);

		block->free = false;

		//std::cout << "Allocated block at offset " << block->offset << " with size " << block->size << std::endl;

		return block;
	}

	void free(Block* block)
	{
		//std::cout << "Freed block at offset " << block->offset << " with size " << block->size << std::endl;

		block->free = true;
		Block* merged = merge(block);
		insert(merged);
	}


private:
	void insert(Block* b)
	{
		u64 fl, sl;
		mapping(b->size, fl, sl);

		b->next_free = free_list[fl][sl];
		if (b->next_free)
			b->next_free->prev_free = b;

		free_list[fl][sl] = b;

		fl_bitmap |= (1ull << fl);
		sl_bitmap[fl] |= (1ull << sl);
	}

	void remove(Block* b)
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
			sl_bitmap[fl] &= ~(1 << sl);

		if (!sl_bitmap[fl])
			fl_bitmap &= ~(1 << fl);
	}

	Block* merge(Block* b)
	{
		Block* next = b->next_phys;
		if (next && next->free)
		{
			remove(next);
			b->size += next->size;
		}

		pool.free(next);

		Block* prev = b->prev_phys;
		if (prev && prev->free)
		{
			remove(prev);
			prev->size += b->size;
			return prev;
		}

		return b;
	}


	void mapping(u64 size, u64& fl, u64& sl)
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

	void split(Block* b, u64 size)
	{
		if (b->size > size)
		{
			Block* new_block = pool.allocate();
			new_block->offset = b->offset + size;
			new_block->size = b->size - size;
			new_block->prev_phys = b;
			new_block->next_phys = b->next_phys;
			new_block->prev_free = b;
			new_block->next_free = b->next_free;
			new_block->free = true;
			b->size = size;
			b->next_phys = new_block;
			if (new_block->next_phys)
				new_block->next_phys->prev_phys = new_block;

			insert(new_block);
		}
	}

public:
	void print_info()
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

private:
	BlockPool pool;

	u64 fl_bitmap;
	u64 sl_bitmap[FL_SIZE];

	Block* header;

	Block* free_list[FL_SIZE][SL_SIZE];
};

class SignedInteger
{
public:
	SignedInteger(s32 v)
		: a(v)
		, b(v)
		, c(v)
	{
	}

	void operator+=(const SignedInteger& other)
	{
		a += other.a;
		b += other.b;
		c += other.c;
	}

	s64 a = 0;
	s64 b = 0;
	s64 c = 0;
};

int main()
{
	{
		const int poolSize = 1024;
		u8* memory = new u8[poolSize];

		auto now = std::chrono::high_resolution_clock::now();
		
		TLSF allocator;
		allocator.initialize(poolSize);

		for (int i = 0; i < 10000; i++)
		{
			Block* block1 = allocator.allocate(sizeof(SignedInteger) + (size_t)(0), 8);
			Block* block2 = allocator.allocate(sizeof(SignedInteger) + (size_t)(0), 8);
			allocator.free(block1);
			allocator.free(block2);
		}

		std::cout << "time:" << std::chrono::high_resolution_clock::now() - now << std::endl;

		delete[] memory;
		memory = 0;
	}

	{
		auto now = std::chrono::high_resolution_clock::now();

		for (int i = 0; i < 10000; i++)
		{
			SignedInteger* a = new SignedInteger(i);
			SignedInteger* b = new SignedInteger(i);

			delete a;
			delete b;
		}
		
		std::cout << "time:" << std::chrono::high_resolution_clock::now() - now << std::endl;
	}

	return 0;
}