#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include "protocol.h"
#include "msgio.h"
#include "interpretmsg.h"
#include "wrappers.h"
#include "client_list.h"
#include "client.h"

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


static inline int cmd_print_reply(char *buf, size_t size, char *msg, ...) {
	int ret;
	va_list args;
	va_start(args, msg);
	ret = vsnprintf(buf, size, msg, args);
	va_end(args);
	if (ret >= size) {
		buf[size-1] = '\n';
		buf[size-2] = '\r';
		ret = size;
	}
	return ret;
}


/**
	msgto = channel / ( user [ "%" host ] "@" servername )
	msgto =/ (user "%" host ) / targetmask
	msgto =/ nickname / ( nickname "!" user "@" host )
*/
static void *privmsg_cmd(struct irc_client *target, void *args) {
	struct cmd_parse *info = (struct cmd_parse *) args;
	char message[MAX_MSG_SIZE+1];
	snprintf(message, sizeof(message), ":%s!%s@%s PRIVMSG %s :%s\r\n", info->from->nick, info->from->username, info->from->hostname, info->params[0], info->params[1]);
	client_enqueue(&target->write_queue, message);
	ev_async_send(target->ev_loop, &target->async_watcher);
	return NULL;
}

static void *whois_cmd(struct irc_client *target, void *args) {
	struct cmd_parse *info = (struct cmd_parse *) args;
	char message[MAX_MSG_SIZE+1];
	int length;
	length = cmd_print_reply(message, sizeof(message), ":development.yaircd.org " RPL_WHOISUSER " %s %s %s %s * :%s\r\n", info->from->nick, target->nick, target->username, target->public_host, target->realname);
	(void) write_to(info->from, message, length);
	length = cmd_print_reply(message, sizeof(message), ":development.yaircd.org " RPL_WHOISSERVER " %s %s %s :%s\r\n", info->from->nick, target->nick, "development.yaircd.org", "I will find you, and I will kill you!");
	(void) write_to(info->from, message, length);
	/* TODO Implement RPL_WHOISIDLE and RPL_WHOISCHANNELS */
	length = cmd_print_reply(message, sizeof(message), ":development.yaircd.org " RPL_ENDOFWHOIS " %s %s :End of WHOIS list\r\n", info->from->nick, target->nick);
	(void) write_to(info->from, message, length);
	return NULL;
}

/** Interprets an IRC message. Assumes that the message is syntactically correct, that is, `parse_msg()` did not return an error condition.
	@param client Pointer to the client where this message came from.
	@param prefix Pointer to a null terminated characters sequence that denotes the prefix of the message, as returned by `parse_msg()` [OPTIONAL].
	@param cmd Pointer to a null terminated characters sequence that denotes the command part of the message, as returned by `parse_msg()`.
	@param params Array of pointers to the command parameters filled by `parse_msg()`.
	@param params_size How many parameters are stored in `params`. This must be an integer greater than or equal to 0.
	@return `1` If the interpreted command shall result in client disconnection from server (i.e., client issued a QUIT command); `0` otherwise.
	@note ERR_NICKCOLLISION is not considered here, because no server links exist yet.
	@todo Implement QUIT command, and the other commands as well
*/
int interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size) {
	int status;
	struct cmd_parse wrapper;
	if (!client->is_registered) {
		if (strcasecmp(cmd, ==, "nick")) {
			if (params_size < 1) {
				send_err_nonicknamegiven(client);
				return 0;
			}
			if (strlen(params[0]) > MAX_NICK_LENGTH) {
				send_err_erroneusnickname(client, params[0]);
				return 0;
			}
			switch (client_list_add(client, params[0])) {
				case CLIENT_LST_INVALID_NICK:
					send_err_erroneusnickname(client, params[0]);
					return 0;
				case CLIENT_LST_NO_MEM:
					pthread_exit(NULL);
				case CLIENT_LST_ALREADY_EXISTS:
					send_err_nicknameinuse(client, params[0]);
					return 0;
			}
			if (client->nick != NULL) {
				free(client->nick);
			}
			if ((client->nick = strdup(params[0])) == NULL) {
				/* No memory for this client's nick, sorry! */
				client_list_delete(client);
				pthread_exit(NULL);
			}
		}
		else if (strcasecmp(cmd, ==, "user")) {
			if (params_size < 4) {
				send_err_needmoreparams(client, cmd);
				return 0;
			}
			if ((client->username = strdup(params[0])) == NULL || (client->realname = strdup(params[3])) == NULL) {
				if (client->nick != NULL) {
					client_list_delete(client);
				}
				pthread_exit(NULL);
			}
		}
		else {
			send_err_notregistered(client);
		}
		if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
			client->is_registered = 1;
			send_welcome(client);
			send_motd(client);
		}
		return 0;
	}
	else {
		/* Interpret message from registered client */
		if (strcasecmp(cmd, ==, "user")) {
			send_err_alreadyregistred(client);
			return 0;
		}
		if (strcasecmp(cmd, ==, "quit")) {
			if (params_size > 0) {
				/* We don't check for strdup returning NULL: if it happens, it's not
				   a big deal - he will just quit with the default message.
				 */
				/*client->quit_msg = strdup(params[0]);*/
			}
			pthread_exit(NULL); /* calls destroy_client() */
		}
		if (strcasecmp(cmd, ==, "privmsg")) {
			if (params_size == 0) {
				send_err_norecipient(client, "PRIVMSG");
				return 0;
			}
			if (params_size == 1) {
				send_err_notexttosend(client);
				return 0;
			}
			/* assert: params_size >= 2 */
			wrapper.from = client;
			wrapper.prefix = prefix;
			wrapper.cmd = cmd;
			wrapper.params = params;
			wrapper.params_size = params_size;
			(void) client_list_find_and_execute(params[0], privmsg_cmd, (void *) &wrapper, &status);
			if (status == 0) {
				send_err_nosuchnick(client, params[0]);
			}
		}
		if (strcasecmp(cmd, ==, "whois")) {
			if (params_size == 0) {
				send_err_nonicknamegiven(client);
				return 0;
			}
			wrapper.from = client;
			wrapper.prefix = prefix;
			wrapper.cmd = cmd;
			wrapper.params = params;
			wrapper.params_size = params_size;
			(void) client_list_find_and_execute(params[0], whois_cmd, (void *) &wrapper, &status);
			if (status == 0) {
				send_err_nosuchnick(client, params[0]);
			}
		}
		/* ... */
		return 0;
	}
}
