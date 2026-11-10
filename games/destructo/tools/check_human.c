/* destructo_check (tools/checkcart.c): humancheck.c as the cart's "human" check, its main() as tool_human and its printf()
 * lines into the cart's output (checkout.h) */
#define DESTRUCTO_CHECK_TOOL 1
#include "checkout.h"
#define main tool_human
#include "humancheck.c"
