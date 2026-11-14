#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <thermite.h>

void server_init(tenv* env);
void server_connect(tenv* env);
void server_poll(tenv* env);
void server_destroy(tenv* env);

void server_list_init(tenv* env);
void server_list_fetch(tenv* env);
void server_list_poll(tenv* env);
void server_list_destroy(tenv* env);
void server_list_start_ping(tenv* env);

/* Favorite servers, saved to server_favs.txt. Keyed by "ip:port". */
bool server_fav_is(const char* ip);
void server_fav_toggle(const char* ip);

/* Name of built-in custom server number idx (idx < custom_count). */
const char* server_custom_name(int idx);

/* Two-letter country code ("JP", "US", ...) for "ip" or "ip:port", or NULL
   when the host isn't known. */
const char* server_country(const char* ip_port);

/* True if exactly this "ip:port" is a Battledome-type arena server
   (Eyes back is disabled there). */
bool server_is_battledome(const char* ip_port);

#endif
