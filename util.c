#include "util.h"

// hackish way to to clear the screen
// Need to find API way
void clearScreen(void)
{
    for(int i = 0; i < 32; i++)
    {
        jo_printf(0, i, "                                          ");
    }
}
