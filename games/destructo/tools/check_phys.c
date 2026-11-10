/* destructo_check (tools/checkcart.c): physcheck.c as the cart's "phys" check, its main() as tool_phys and its printf()
 * lines into the cart's output (checkout.h) */
#define DESTRUCTO_CHECK_TOOL 1
#include "checkout.h"
#define main tool_phys
#include "physcheck.c"
