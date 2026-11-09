#ifndef REALTIME_LB_H
#define REALTIME_LB_H

#include <thermite.h>

/* In-game "Realtime Leaderboards": while the screen is open THIS DEVICE visits
   every server (join, read the top 10, leave) and shows the results with the
   server's country, address and player count. Everything stops when the
   screen is closed or when Play is pressed (ui_realtime_lb_close). */
void ui_realtime_lb_open(tenv *env);
void ui_realtime_lb(tenv *env);
void ui_realtime_lb_close(tenv *env); /* safe to call any time */

#endif
