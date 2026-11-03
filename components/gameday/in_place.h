// Puts an object back to its freshly constructed state where it sits.
//
// `x = T{}` builds the new T on the stack first and copies it over. A
// GameSnapshot is 1.2 KB and a Job 1.6 KB, and the main loop clears both on
// the way from a Favorites switch to the display, so those temporaries were
// the biggest frames on the loop task's stack. Destroying and constructing in
// place needs no temporary. Only for types whose default constructor cannot
// throw, which is every plain data struct here.
#pragma once

#include <new>

namespace espn {

template<class T> inline void reset_in_place(T &x) {
  x.~T();
  new (&x) T();
}

}  // namespace espn
