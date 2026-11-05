/* RECOMP_CHECK in C++, as the menu (src/menu) is built: test_main.c calls
 * this, and checks its line. */
#include "platform/recomp_check.h"

extern "C" int recomp_check_from_cpp(int rows)
{
    return RECOMP_CHECK(rows <= 4, "%d rows, room for 4", rows);
}
