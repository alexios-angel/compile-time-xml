#ifndef CTLL__FIXED_STRING__GPP
#define CTLL__FIXED_STRING__GPP

#ifndef CTLL_IN_A_MODULE
#include <utility>
#include <cstddef>
#include <string_view>
#include <array>
#include <cstdint>
#include <iosfwd>
#endif

#include "utilities.hpp"

namespace ctll {

struct length_value_t {
	uint32_t value;
	uint8_t length;
};

constexpr length_value_t length_and_value_of_utf8_code_point(uint8_t first_unit) noexcept {
	if ((first_unit & 0b1000'0000) == 0b0000'0000) return {static_cast<uint32_t>(first_unit), 1};
	else if ((first_unit & 0b1110'0000) == 0b1100'0000) return {static_cast<uint32_t>(first_unit & 0b0001'1111), 2};
	else if ((first_unit & 0b1111'0000) == 0b1110'0000) return {static_cast<uint32_t>(first_unit & 0b0000'1111), 3};
	else if ((first_unit & 0b1111'1000) == 0b1111'0000) return {static_cast<uint32_t>(first_unit & 0b0000'0111), 4};
	else if ((first_unit & 0b1111'1100) == 0b1111'1000) return {static_cast<uint32_t>(first_unit & 0b0000'0011), 5};
	else if ((first_unit & 0b1111'1100) == 0b1111'1100) return {static_cast<uint32_t>(first_unit & 0b0000'0001), 6};
	else return {0, 0};
}

constexpr char32_t value_of_trailing_utf8_code_point(uint8_t unit, bool & correct) noexcept {
	if ((unit & 0b1100'0000) == 0b1000'0000) return unit & 0b0011'1111;
	else {
		correct = false;
		return 0;
	}
}

constexpr length_value_t length_and_value_of_utf16_code_point(uint16_t first_unit) noexcept {
	if ((first_unit & 0b1111110000000000) == 0b1101'1000'0000'0000) return {static_cast<uint32_t>(first_unit & 0b0000001111111111), 2};
	else return {first_unit, 1};
}

struct construct_from_pointer_t { };

constexpr auto construct_from_pointer = construct_from_pointer_t{};

template <typename> inline constexpr bool always_false_v = false;

// A general purpose compile-time string.
//
// The content is stored as UTF-32 code points: construction from char8_t
// (and, with CTRE_STRING_IS_UTF8, from char) decodes UTF-8, construction
// from char16_t decodes UTF-16, and wchar_t/char32_t units are taken as
// code points. `N` is the capacity in input code units; `size()` is the
// decoded length, which can be smaller. A malformed encoding doesn't fail
// the compilation by itself, it just sets `correct()` to false.
//
// The type is structural (all members public), so values can be used as
// non-type template parameters in C++20.
CTLL_EXPORT template <size_t N> struct fixed_string {
	using value_type = char32_t;
	using size_type = size_t;
	using const_reference = const char32_t &;
	using const_iterator = const char32_t *;
	using iterator = const_iterator;

	static constexpr size_t npos = static_cast<size_t>(-1);

	char32_t content[N ? N : 1] = {};
	size_t real_size{0};
	bool correct_flag{true};

	constexpr fixed_string() noexcept = default;

	template <typename T> constexpr fixed_string(construct_from_pointer_t, const T * input) noexcept {
		if constexpr (std::is_same_v<T, char>) {
			#ifdef CTRE_STRING_IS_UTF8
				size_t out{0};
				for (size_t i{0}; i < N; ++i) {
					length_value_t info = length_and_value_of_utf8_code_point(input[i]);
					switch (info.length) {
						case 6:
							if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
							[[fallthrough]];
						case 5:
							if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
							[[fallthrough]];
						case 4:
							if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
							[[fallthrough]];
						case 3:
							if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
							[[fallthrough]];
						case 2:
							if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
							[[fallthrough]];
						case 1:
							content[out++] = static_cast<char32_t>(info.value);
							real_size++;
							break;
						default:
							correct_flag = false;
							return;
					}
				}
			#else
				for (size_t i{0}; i < N; ++i) {
					content[i] = static_cast<uint8_t>(input[i]);
					real_size++;
				}
			#endif
#if defined(__cpp_char8_t)
		} else if constexpr (std::is_same_v<T, char8_t>) {
			size_t out{0};
			for (size_t i{0}; i < N; ++i) {
				length_value_t info = length_and_value_of_utf8_code_point(input[i]);
				switch (info.length) {
					case 6:
						if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
						[[fallthrough]];
					case 5:
						if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
						[[fallthrough]];
					case 4:
						if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
						[[fallthrough]];
					case 3:
						if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
						[[fallthrough]];
					case 2:
						if (++i < N) info.value = (info.value << 6) | value_of_trailing_utf8_code_point(input[i], correct_flag);
						[[fallthrough]];
					case 1:
						content[out++] = static_cast<char32_t>(info.value);
						real_size++;
						break;
					default:
						correct_flag = false;
						return;
				}
			}
#endif
		} else if constexpr (std::is_same_v<T, char16_t>) {
			size_t out{0};
			for (size_t i{0}; i < N; ++i) {
				length_value_t info = length_and_value_of_utf16_code_point(input[i]);
				if (info.length == 2) {
					if (++i < N) {
						if ((input[i] & 0b1111'1100'0000'0000) == 0b1101'1100'0000'0000) {
							content[out++] = ((info.value << 10) | (input[i] & 0b0000'0011'1111'1111)) + 0x10000;
						} else {
							correct_flag = false;
							break;
						}
					}
				} else {
					content[out++] = info.value;
				}
			}
			real_size = out;
		} else if constexpr (std::is_same_v<T, wchar_t> || std::is_same_v<T, char32_t>) {
			for (size_t i{0}; i < N; ++i) {
				content[i] = static_cast<char32_t>(input[i]);
				real_size++;
			}
		} else {
			static_assert(always_false_v<T>, "ctll::fixed_string must be constructed from a character type (char, char8_t, char16_t, char32_t, wchar_t)");
		}
	}

	template <typename T> constexpr fixed_string(const std::array<T, N> & in) noexcept: fixed_string{construct_from_pointer, in.data()} { }
	template <typename T> constexpr fixed_string(const T (&input)[N+1]) noexcept: fixed_string{construct_from_pointer, input} { }

	constexpr fixed_string(const fixed_string & other) noexcept {
		for (size_t i{0}; i < N; ++i) {
			content[i] = other.content[i];
		}
		real_size = other.real_size;
		correct_flag = other.correct_flag;
	}
	constexpr fixed_string & operator=(const fixed_string & other) noexcept {
		for (size_t i{0}; i < N; ++i) {
			content[i] = other.content[i];
		}
		real_size = other.real_size;
		correct_flag = other.correct_flag;
		return *this;
	}

	// observers
	constexpr bool correct() const noexcept {
		return correct_flag;
	}
	constexpr size_t size() const noexcept {
		return real_size;
	}
	constexpr size_t length() const noexcept {
		return real_size;
	}
	static constexpr size_t max_size() noexcept {
		return N;
	}
	static constexpr size_t capacity() noexcept {
		return N;
	}
	constexpr bool empty() const noexcept {
		return real_size == 0;
	}

	// element access
	constexpr const char32_t * data() const noexcept {
		return content;
	}
	constexpr const char32_t * begin() const noexcept {
		return content;
	}
	constexpr const char32_t * end() const noexcept {
		return content + size();
	}
	constexpr const char32_t * cbegin() const noexcept {
		return begin();
	}
	constexpr const char32_t * cend() const noexcept {
		return end();
	}
	constexpr char32_t operator[](size_t i) const noexcept {
		return content[i];
	}
	constexpr char32_t front() const noexcept {
		return content[0];
	}
	constexpr char32_t back() const noexcept {
		return content[real_size - 1];
	}

	// conversion
	constexpr std::basic_string_view<char32_t> view() const noexcept {
		return std::basic_string_view<char32_t>{content, size()};
	}
	constexpr operator std::basic_string_view<char32_t>() const noexcept {
		return view();
	}

	// comparison
	template <size_t M> constexpr bool is_same_as(const fixed_string<M> & rhs) const noexcept {
		if (real_size != rhs.size()) return false;
		for (size_t i{0}; i != real_size; ++i) {
			if (content[i] != rhs[i]) return false;
		}
		return true;
	}
	// three-way lexicographical comparison, ala std::basic_string_view::compare
	template <size_t M> constexpr int compare(const fixed_string<M> & rhs) const noexcept {
		const size_t common = real_size < rhs.size() ? real_size : rhs.size();
		for (size_t i{0}; i != common; ++i) {
			if (content[i] < rhs[i]) return -1;
			if (content[i] > rhs[i]) return 1;
		}
		if (real_size < rhs.size()) return -1;
		if (real_size > rhs.size()) return 1;
		return 0;
	}

	// search
	template <size_t M> constexpr size_t find(const fixed_string<M> & needle, size_t pos = 0) const noexcept {
		if (pos > real_size) return npos;
		if (needle.size() > real_size) return npos;
		for (size_t i{pos}; i + needle.size() <= real_size; ++i) {
			bool found = true;
			for (size_t j{0}; j != needle.size(); ++j) {
				if (content[i + j] != needle[j]) {
					found = false;
					break;
				}
			}
			if (found) return i;
		}
		return npos;
	}
	constexpr size_t find(char32_t needle, size_t pos = 0) const noexcept {
		for (size_t i{pos}; i < real_size; ++i) {
			if (content[i] == needle) return i;
		}
		return npos;
	}
	template <size_t M> constexpr bool contains(const fixed_string<M> & needle) const noexcept {
		return find(needle) != npos;
	}
	constexpr bool contains(char32_t needle) const noexcept {
		return find(needle) != npos;
	}
	template <size_t M> constexpr bool starts_with(const fixed_string<M> & prefix) const noexcept {
		if (prefix.size() > real_size) return false;
		for (size_t i{0}; i != prefix.size(); ++i) {
			if (content[i] != prefix[i]) return false;
		}
		return true;
	}
	constexpr bool starts_with(char32_t c) const noexcept {
		return real_size != 0 && content[0] == c;
	}
	template <size_t M> constexpr bool ends_with(const fixed_string<M> & suffix) const noexcept {
		if (suffix.size() > real_size) return false;
		const size_t offset = real_size - suffix.size();
		for (size_t i{0}; i != suffix.size(); ++i) {
			if (content[offset + i] != suffix[i]) return false;
		}
		return true;
	}
	constexpr bool ends_with(char32_t c) const noexcept {
		return real_size != 0 && content[real_size - 1] == c;
	}

	// substring: capacity is computed at compile time, content is clamped
	// to the decoded size
	template <size_t Pos, size_t Count = npos> constexpr auto substr() const noexcept {
		constexpr size_t available_capacity = (Pos < N) ? (N - Pos) : 0;
		constexpr size_t result_capacity = (Count < available_capacity) ? Count : available_capacity;
		fixed_string<result_capacity> result;
		size_t out{0};
		for (size_t i{Pos}; i < real_size && out != result_capacity; ++i, ++out) {
			result.content[out] = content[i];
		}
		result.real_size = out;
		result.correct_flag = correct_flag;
		return result;
	}
};

template <typename CharT, size_t N> fixed_string(const CharT (&)[N]) -> fixed_string<N-1>;
template <typename CharT, size_t N> fixed_string(const std::array<CharT,N> &) -> fixed_string<N>;

template <size_t N> fixed_string(fixed_string<N>) -> fixed_string<N>;

// equality (works across different capacities; compares decoded content)
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator==(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return lhs.is_same_as(rhs);
}
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator!=(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return !lhs.is_same_as(rhs);
}
CTLL_EXPORT template <size_t A, typename CharT, size_t M> constexpr bool operator==(const fixed_string<A> & lhs, const CharT (&rhs)[M]) noexcept {
	return lhs.is_same_as(fixed_string<M-1>(rhs));
}
CTLL_EXPORT template <size_t A, typename CharT, size_t M> constexpr bool operator!=(const fixed_string<A> & lhs, const CharT (&rhs)[M]) noexcept {
	return !lhs.is_same_as(fixed_string<M-1>(rhs));
}
CTLL_EXPORT template <typename CharT, size_t M, size_t B> constexpr bool operator==(const CharT (&lhs)[M], const fixed_string<B> & rhs) noexcept {
	return rhs.is_same_as(fixed_string<M-1>(lhs));
}
CTLL_EXPORT template <typename CharT, size_t M, size_t B> constexpr bool operator!=(const CharT (&lhs)[M], const fixed_string<B> & rhs) noexcept {
	return !rhs.is_same_as(fixed_string<M-1>(lhs));
}

// lexicographical ordering
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator<(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return lhs.compare(rhs) < 0;
}
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator<=(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return lhs.compare(rhs) <= 0;
}
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator>(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return lhs.compare(rhs) > 0;
}
CTLL_EXPORT template <size_t A, size_t B> constexpr bool operator>=(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	return lhs.compare(rhs) >= 0;
}

// concatenation
CTLL_EXPORT template <size_t A, size_t B> constexpr auto operator+(const fixed_string<A> & lhs, const fixed_string<B> & rhs) noexcept {
	fixed_string<A + B> result;
	size_t out{0};
	for (size_t i{0}; i != lhs.size(); ++i) {
		result.content[out++] = lhs[i];
	}
	for (size_t i{0}; i != rhs.size(); ++i) {
		result.content[out++] = rhs[i];
	}
	result.real_size = out;
	result.correct_flag = lhs.correct() && rhs.correct();
	return result;
}
CTLL_EXPORT template <size_t A, typename CharT, size_t M> constexpr auto operator+(const fixed_string<A> & lhs, const CharT (&rhs)[M]) noexcept {
	return lhs + fixed_string<M-1>(rhs);
}
CTLL_EXPORT template <typename CharT, size_t M, size_t B> constexpr auto operator+(const CharT (&lhs)[M], const fixed_string<B> & rhs) noexcept {
	return fixed_string<M-1>(lhs) + rhs;
}

// iostream interoperability: the content is encoded as UTF-8 and written
// with a single formatted output operation (so padding/width apply once)
CTLL_EXPORT template <typename Traits, size_t N> std::basic_ostream<char, Traits> & operator<<(std::basic_ostream<char, Traits> & stream, const fixed_string<N> & string) {
	char buffer[4 * (N ? N : 1)];
	size_t out{0};
	for (size_t i{0}; i != string.size(); ++i) {
		const char32_t cp = string[i];
		if (cp < 0x80) {
			buffer[out++] = static_cast<char>(cp);
		} else if (cp < 0x800) {
			buffer[out++] = static_cast<char>(0xC0 | (cp >> 6));
			buffer[out++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			buffer[out++] = static_cast<char>(0xE0 | (cp >> 12));
			buffer[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			buffer[out++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			buffer[out++] = static_cast<char>(0xF0 | (cp >> 18));
			buffer[out++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			buffer[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			buffer[out++] = static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	return stream << std::basic_string_view<char, Traits>(buffer, out);
}

}

#endif
