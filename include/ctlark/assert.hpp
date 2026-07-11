#ifndef CTLARK__ASSERT__HPP
#define CTLARK__ASSERT__HPP

#ifndef CTLARK_IN_A_MODULE
#include <cstdlib>
#endif

// Internal invariant checks that work during constant evaluation.
//
// Define CTLARK_DEBUG to enable them. When a check fails while the
// compiler is evaluating a constexpr parse, evaluation stops with an
// error pointing at the CTLARK_CONSTEXPR_ASSERT line: the call to
// constexpr_assert_failed is the diagnostic, because that function is
// deliberately not constexpr and the compiler quotes the call - with
// the message literal - when it rejects it. The same check running at
// runtime aborts. Without CTLARK_DEBUG the checks compile away
// entirely.

namespace ctlark::detail {

// not constexpr, on purpose: reaching this call during constant
// evaluation is what produces the compiler error
[[noreturn]] inline void constexpr_assert_failed(const char * /*msg*/) noexcept {
	std::abort();
}

} // namespace ctlark::detail

#ifdef CTLARK_DEBUG
#define CTLARK_CONSTEXPR_ASSERT(cond, msg) \
	do { \
		if (!(cond)) { ::ctlark::detail::constexpr_assert_failed(msg); } \
	} while (false)
#else
#define CTLARK_CONSTEXPR_ASSERT(cond, msg) ((void)0)
#endif

#endif
