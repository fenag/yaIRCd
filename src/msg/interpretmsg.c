#include <string.h>
#include "protocol.h"
#include "sendreply.h"
#include "interpretmsg.h"
#include "wrappers.h"
#include "client_list.h"

/** @file
	@brief Functions responsible for interpreting an IRC message.

	This file implements functions that are responsible for interpreting an IRC message.
	The goal is to provide an abstract function that shall be called everytime a new message arrived from a client. That function
	will interpret the message, decide if it's valid, and generate the appropriate command reply, or error reply if it was an invalid
	message.
	
	@author Filipe Goncalves
	@date November 2013
	@see parsemsg.c
	@todo Commands are now recognized by serial comparison. Discuss if a trie approach is benefitial.
*/

/** Interprets an IRC message. Assumes that the message is syntactically correct, that is, `parse_msg()` did not return an error condition.
	@param client Pointer to the client where this message came from.
	@param prefix Pointer to a null terminated characters sequence that denotes the prefix of the message, as returned by `parse_msg()` [OPTIONAL].
	@param cmd Pointer to a null terminated characters sequence that denotes the command part of the message, as returned by `parse_msg()`.
	@param params Array of pointers to the command parameters filled by `parse_msg()`.
	@param params_size How many parameters are stored in `params`. This must be an integer greater than or equal to 0.
	@param has_prefix `1` if `prefix` points to a valid prefix; `0` otherwise (the message has no prefix).
	@return `1` If the interpreted command shall result in client disconnection from server (i.e., client issued a QUIT command); `0` otherwise.
	@note ERR_NICKCOLLISION is not considered here, because no server links exist yet.
	@todo Implement ERR_NEEDMOREPARAMS and ERR_ALREADYREGISTRED in USER command
	@todo Add and delete clients from clients list
	@todo Implement QUIT command
*/
int interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size, int has_prefix) {
	if (!client->is_registered) {
		if (strcasecmp(cmd, ==, "nick")) {
			if (params_size < 1) {
				send_err_nonicknamegiven(client);
			}
			else {
				if (client_list_find_by_nick(params[0]) != NULL) {
					/* TODO Send ERR_NICKNAMEINUSE */
				}
				else if (client_list_add(client, params[0]) == -1) {
					/* TODO Send ERR_ERRONEUSNICKNAME */
				}
				else {
					client->nick = strdup(params[0]);
				}
			}
		}
		else if (strcasecmp(cmd, ==, "user")) {
			if (params_size < 4) {
				send_err_needmoreparams(client, cmd);
			}
			else {
				client->username = strdup(params[0]);
				client->realname = strdup(params[3]);
			}
		}
		else {
			send_err_notregistered(client);
		}
		if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
			client->is_registered = 1;
		}
		return 0;
	}
	else {
		/* Interpret message from registered client */
		return 0;
	}
}
