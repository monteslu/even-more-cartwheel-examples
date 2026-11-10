/* destructo_check (tools/checkcart.c): soak.c as the cart's "soak" check, its main() as tool_soak and its printf()
 * lines into the cart's output (checkout.h) */
#define DESTRUCTO_CHECK_TOOL 1
#include "checkout.h"
#define main tool_soak
#include "../src/soak.c"
