/* destructo_check (tools/checkcart.c): levelcheck.c as the cart's "levels" check, its main() as tool_levels and its printf()
 * lines into the cart's output (checkout.h) */
#define DESTRUCTO_CHECK_TOOL 1
#include "checkout.h"
#define main tool_levels
#include "levelcheck.c"
