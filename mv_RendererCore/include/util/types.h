#ifndef _MV_TYPES_H_
#define _MV_TYPES_H_

#include "type_traits"

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

	namespace enum_concept
	{
		template<typename T>
		struct has_bitwise_operators : std::false_type {};
		template<typename T>
		struct has_and_or_operators : has_bitwise_operators<T> {};
	}


	namespace type_traits
	{
		template<bool con> using concept_t = typename std::enable_if<con, std::nullptr_t>::type;
		template<typename T> using underlying_type_t = typename std::underlying_type<T>::type;
	}

	namespace detail
	{
		using namespace type_traits;
		template<typename T, concept_t<std::is_enum<T>::value> = nullptr>
		constexpr underlying_type_t<T> underlying_cast(T e) { return static_cast<underlying_type_t<T>>(e); }
	}

	template<typename T, type_traits::concept_t<enum_concept::has_and_or_operators<T>::value> = nullptr>
	constexpr T operator&(T lhs, T rhs) { return static_cast<T>(detail::underlying_cast(lhs) & detail::underlying_cast(rhs)); }
	template<typename T, type_traits::concept_t<enum_concept::has_and_or_operators<T>::value> = nullptr>
	T& operator&=(T& lhs, T rhs) { lhs = lhs & rhs; return lhs; }

	template<typename T, type_traits::concept_t<enum_concept::has_and_or_operators<T>::value> = nullptr>
	constexpr T operator|(T lhs, T rhs) { return static_cast<T>(detail::underlying_cast(lhs) | detail::underlying_cast(rhs)); }
	template<typename T, type_traits::concept_t<enum_concept::has_and_or_operators<T>::value> = nullptr>
	T& operator|=(T& lhs, T rhs) { lhs = lhs | rhs; return lhs; }
}

#endif
