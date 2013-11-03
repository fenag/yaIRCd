#ifndef __IRC_CLIENT_LIST_GUARD__
#define __IRC_CLIENT_LIST_GUARD__
#include "client.h"

struct irc_client *client_list_find_by_nick(char *nick);
void client_list_add(struct irc_client *new_client);
void client_list_delete(struct irc_client *client);
void notify_all_clients(char *msg, char *nick, int msg_size);

#endif /* __IRC_CLIENT_LIST_GUARD__ */
