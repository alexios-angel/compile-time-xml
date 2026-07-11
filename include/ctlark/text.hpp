#ifndef CTLARK__TEXT__HPP
#define CTLARK__TEXT__HPP

#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// A compile-time string as a type: one non-type template parameter per
// character. Used for names and token values throughout - two texts
// with the same characters are the same TYPE, which is what makes
// name comparisons and tree lookups plain type equality.

namespace ctlark {

template <auto... Chars> struct text {
	// null-terminated so c_str()/data() work as C strings; size() excludes it
	static constexpr char storage[sizeof...(Chars) + 1]{static_cast<char>(Chars)..., '\0'};

	static constexpr const char * c_str() noexcept {
		return storage;
	}
	static constexpr size_t size() noexcept {
		return sizeof...(Chars);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Chars) == 0;
	}
	static constexpr std::string_view view() noexcept {
		return std::string_view{storage, sizeof...(Chars)};
	}
	constexpr operator std::string_view() const noexcept {
		return view();
	}
	template <auto... Rhs> constexpr bool operator==(text<Rhs...>) const noexcept {
		return view() == text<Rhs...>::view();
	}
	friend constexpr bool operator==(text, std::string_view rhs) noexcept {
		return view() == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, text) noexcept {
		return lhs == view();
	}
#if __cplusplus < 202002L
	template <auto... Rhs> constexpr bool operator!=(text<Rhs...>) const noexcept {
		return view() != text<Rhs...>::view();
	}
	friend constexpr bool operator!=(text, std::string_view rhs) noexcept {
		return view() != rhs;
	}
	friend constexpr bool operator!=(std::string_view lhs, text) noexcept {
		return lhs != view();
	}
#endif
};

} // namespace ctlark

#endif
