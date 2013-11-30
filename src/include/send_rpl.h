#ifndef __YAIRCD_SEND_RPL_GUARD__
#define __YAIRCD_SEND_RPL_GUARD__
#include "client.h"

/** @file
	@brief Functions that send a reply to a command issued by an IRC user
	
	This file provides a set of functions to send various replies to an IRC user in the sequence of a command sent to the server.
	
	@author Filipe Goncalves
	@date November 2013
*/

/* Functions documented in the source file */
void send_motd(struct irc_client *client);
void send_welcome(struct irc_client *client);
void notify_privmsg(struct irc_client *from, struct irc_client *to, char *dest, char *message);

#endif /* __YAIRCD_SEND_RPL_GUARD__ */
