/* The check cart's output (tools/checkcart.c): force-included into each check tool compiled into destructo_check, so the
 * tool's printf() lines land in the cart's output buffer, which the gates read over the debug channel (check out). */
#ifndef DESTRUCTO_CHECKOUT_H
#define DESTRUCTO_CHECKOUT_H
#include <stdio.h>
int check_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef DESTRUCTO_CHECK_TOOL
#define printf check_printf
#endif
#endif
