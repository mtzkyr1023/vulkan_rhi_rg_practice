
#include "bit"

#include "memory/tlsf_allocator.h"

namespace mv::memory
{
	TLSF::TLSF()
		: head_(nullptr)
	{
	}

	TLSF::~TLSF()
	{
	}

	void TLSF::initialize(u64 size)
	{
		head_ = new Block{ 0, size, nullptr, nullptr, true };
		insert(head_);
	}

	Block* TLSF::allocate(u64 size, u64 alignment)
	{
		u64 fli = std::countl_zero(size);
	}

	void TLSF::free(Block* b)
	{

	}

	void TLSF::insert(Block* b)
	{

	}

	void TLSF::remove(Block* b)
	{

	}

	void TLSF::merge(Block* a, Block* b)
	{

	}
}
