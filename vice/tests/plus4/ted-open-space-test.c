/* This checks initialization of VICE's existing fallback buffer, not the
   electrical values of an undriven TED bus. */
#include <assert.h>
#include <stdio.h>

#include "plus4mem.h"

int main(void)
{
    uint8_t *p = mem_get_open_space();
    assert(p[0] == 0 && p[320] == 0xff);
    assert(p[1] != 0 && p[0xffff] != 0);
    p[1] = 0x42;
    assert(mem_get_open_space() == p);
    assert(p[1] == 0x42);
    puts("TED open-space fallback initializes once");
    return 0;
}
