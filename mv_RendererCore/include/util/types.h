#ifndef _MV_TYPES_H_
#define _MV_TYPES_H_

namespace mv
{
	namespace types
	{
		using u8 = unsigned char;
		using u16 = unsigned short;
		using u32 = unsigned int;
		using u64 = unsigned long long;
		using s8 = signed char;
		using s16 = signed short;
		using s32 = signed int;
		using s64 = signed long long;
		using f32 = float;
		using f64 = double;
	}

	static const types::u32 INVALID_HANDLE = 0xFFFFFFFF;
}

#endif
