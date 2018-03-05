#ifndef DEBUG_H_
#define DEBUG_H_

#include <cassert>

inline void Assert(bool expr) {
	if (expr) return;
	// Add a breakpoint here?
	assert(false);
}

#undef assert

#endif // !DEBUG_H_

