#ifndef NTL_BOT_ADAPTER_H
#define NTL_BOT_ADAPTER_H

#include <thermite.h>

/* Same call contract as sbot_go(): reads real game state, drives the
 * NTL bot core, and writes the result into gdata->bot.output.xm/ym/accel
 * -- the same fields sbot_go() already writes, so input.c and everything
 * downstream of it needs no changes. */
void ntl_bot_adapter_go(tenv* env);

#endif
