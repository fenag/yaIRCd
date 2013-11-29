#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "protocol.h"
#include "msgio.h"
#include "interpretmsg.h"
#include "wrappers.h"
#include "client_list.h"
#include "client.h"
#include "channel.h"
#include "serverinfo.h"
#include "trie.h"

/** @file
   @brief Functions responsible for interpreting an IRC message.
   This file implements functions that are responsible for interpreting an IRC message.
   The goal is to provide an abstract function that shall be called everytime a new message arrived from a client. That
   function will interpret the message, decide if it's valid, and generate the appropriate command reply, or error
   reply if it was an invalid message.
   @author Filipe Goncalves
   @date November 2013
   @see parsemsg.c
   @todo Commands are now recognized by serial comparison. Discuss if a trie approach is benefitial.
 */

#define array_count(a) (sizeof(a)/sizeof(*a))
 
static struct trie_t *commands;
static struct trie_t *commands_unregistered;

struct cmd_func {
	char *command;
	void (*f)(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
};

void cmd_nick_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size) {
	if (params_size < 1) {
		send_err_nonicknamegiven(client);
		return;
	}
	if (strlen(params[0]) > MAX_NICK_LENGTH) {
		send_err_erroneusnickname(client, params[0]);
		return;
	}
	switch (client_list_add(client, params[0])) {
		case LST_INVALID_WORD:
			send_err_erroneusnickname(client, params[0]);
			return;
		case LST_NO_MEM:
			pthread_exit(NULL);
		case LST_ALREADY_EXISTS:
			send_err_nicknameinuse(client, params[0]);
			return;
	}
	if (client->nick != NULL) {
		client_list_delete(client);
		free(client->nick);
	}
	if ((client->nick = strdup(params[0])) == NULL) {
		/* No memory for this client's nick, sorry! */
		client_list_delete(client);
		pthread_exit(NULL);
	}
	if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
		client->is_registered = 1;
		send_welcome(client);
		send_motd(client);
	}
}

void cmd_user_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size) {
	if (params_size < 4) {
		send_err_needmoreparams(client, cmd);
		return;
	}
	if (client->username != NULL) {
		free(client->username);
	}
	if (client->realname != NULL) {
		free(client->realname);
	}
	if ((client->username = strdup(params[0])) == NULL || 
		(client->realname = strdup(params[3])) == NULL) {
		if (client->nick != NULL) {
			client_list_delete(client);
		}
		pthread_exit(NULL);
	}
	if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
		client->is_registered = 1;
		send_welcome(client);
		send_motd(client);
	}
}

static struct cmd_func cmds_unregistered[] = {
	{ "nick", cmd_nick_unregistered },
	{ "user", cmd_user_unregistered }
};
 
static int is_valid(char c) {
	return isalpha((unsigned char) c);
}

static char pos_to_char(int c) {
	return c - 'a';
}

static int char_to_pos(char c) {
	return tolower((unsigned char) c) - 'a';
}

int add_commands(struct trie_t *trie, struct cmd_func *array, size_t array_size) {
	size_t i;
	for (i = 0; i < array_size; i++) {
		if (add_word_trie(trie, array[i].command, (void *) &array[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

int cmds_init(void) {
	int i, j;
	if ((commands = init_trie(NULL, is_valid, pos_to_char, char_to_pos, 'z'-'a'+1)) == NULL) {
		return -1;
	}
	if ((commands_unregistered = init_trie(NULL, is_valid, pos_to_char, char_to_pos, 'z'-'a'+1)) == NULL) {
		free(commands);
		return -1;
	}
	i = add_commands(commands_unregistered, cmds_unregistered, array_count(cmds_unregistered));
	#if 0
	j = add_commands(commands_registered, cmds_registered, array_count(cmds_registered));
	#else
	j = 0;
	#endif
	return -!(i == 0 && j == 0);
}

/**
   TODO add documentation for this
   msgto = channel / ( user [ "%" host ] "@" servername )msgto =/ (user "%" host ) / targetmaskmsgto =/ nickname / (
      nickname "!" user "@" host )
 */
static void *privmsg_cmd(void *target_client, void *args)
{
	struct cmd_parse *info = (struct cmd_parse*)args;
	struct irc_client *target = (struct irc_client*)target_client;
	notify_privmsg(info->from, target, target->nick, info->params[1]);
	return NULL;
}

/** Callback function used by `client_list_find_and_execute()` when a client issues a WHOIS command.
   This function generates and sends the appropriate WHOIS reply
   @param target_client Client being WHOIS'ed
   @param args A parameter of type `struct cmd_parse ` holding information previously obtained by the messages parsing
      routine.
   @return This function always returns `NULL`.
 */
static void *whois_cmd(void *target_client, void *args)
{
	struct cmd_parse *info = (struct cmd_parse*)args;
	struct irc_client *target = (struct irc_client*)target_client;
	char message[MAX_MSG_SIZE + 1];
	int length;
	length = cmd_print_reply(message,
				 sizeof(message),
				 ":%s " RPL_WHOISUSER " %s %s %s %s * :%s\r\n",
				 get_server_name(),
				 info->from->nick,
				 target->nick,
				 target->username,
				 target->public_host,
				 target->realname);
	(void)write_to(info->from, message, length);
	length = cmd_print_reply(message, sizeof(message), ":%s " RPL_WHOISSERVER " %s %s %s :%s\r\n",
				 get_server_name(), info->from->nick, target->nick, get_server_name(), get_server_desc());
	(void)write_to(info->from, message, length);
	/* TODO Implement RPL_WHOISIDLE and RPL_WHOISCHANNELS */
	length = cmd_print_reply(message,
				 sizeof(message),
				 ":%s " RPL_ENDOFWHOIS " %s %s :End of WHOIS list\r\n",
				 get_server_name(),
				 info->from->nick,
				 target->nick);
	(void)write_to(info->from, message, length);
	return NULL;
}

/** Interprets an IRC message. Assumes that the message is syntactically correct, that is, `parse_msg()` did not return
   an error condition.
   @param client Pointer to the client where this message came from.
   @param prefix Pointer to a null terminated characters sequence that denotes the prefix of the message, as returned by
      `parse_msg()` [OPTIONAL].
   @param cmd Pointer to a null terminated characters sequence that denotes the command part of the message, as returned
      by `parse_msg()`.
   @param params Array of pointers to the command parameters filled by `parse_msg()`.
   @param params_size How many parameters are stored in `params`. This must be an integer greater than or equal to 0.
   @return `1` If the interpreted command shall result in client disconnection from server (i.e., client issued a QUIT
      command); `0` otherwise.
   @note ERR_NICKCOLLISION is not considered here, because no server links exist yet.
   @todo Implement QUIT command, and the other commands as well
 */
void interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	int status;
	struct cmd_parse wrapper;
	struct cmd_func *command_func;
	
	if (!client->is_registered) {
		if ((command_func = (struct cmd_func *) find_word_trie(commands_unregistered, cmd)) == NULL) {
			/* Sorry pal, no such command! */
			send_err_notregistered(client);
		}
		else {
			(*command_func->f)(client, prefix, cmd, params, params_size);
		}
	}
	else  {
		/* Interpret message from registered client */
		
		if (strcasecmp(cmd, ==, "user")) {
			send_err_alreadyregistred(client);
			return;
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
				return;
			}
			if (params_size == 1) {
				send_err_notexttosend(client);
				return;
			}
			/* assert: params_size >= 2 */
			wrapper.from = client;
			wrapper.prefix = prefix;
			wrapper.cmd = cmd;
			wrapper.params = params;
			wrapper.params_size = params_size;
			if (*params[0] == '#') {
				if (channel_msg(client, params[0], params[1]) == CHAN_NO_SUCH_CHANNEL) {
					send_err_nosuchnick(client, params[0]);
				}
				return;
			} else {
				(void)client_list_find_and_execute(params[0], privmsg_cmd, (void*)&wrapper, &status);
				if (status == 0) {
					send_err_nosuchnick(client, params[0]);
				}
				return;
			}
		}
		if (strcasecmp(cmd, ==, "whois")) {
			if (params_size == 0) {
				send_err_nonicknamegiven(client);
				return;
			}
			wrapper.from = client;
			wrapper.prefix = prefix;
			wrapper.cmd = cmd;
			wrapper.params = params;
			wrapper.params_size = params_size;
			(void)client_list_find_and_execute(params[0], whois_cmd, (void*)&wrapper, &status);
			if (status == 0) {
				send_err_nosuchnick(client, params[0]);
			}
		}
		if (strcasecmp(cmd, ==, "join")) {
			/*
			        ERR_NEEDMOREPARAMS
			        ERR_INVITEONLYCHAN
			        ERR_CHANNELISFULL
			        ERR_NOSUCHCHANNEL - channel name is invalid
			        ERR_TOOMANYTARGETS
			        ERR_BANNEDFROMCHAN
			        ERR_BADCHANNELKEY
			        ERR_BADCHANMASK
			        ERR_TOOMANYCHANNELS
			        ERR_UNAVAILRESOURCE

			        RPL_TOPIC
			 */
			if (params_size < 1) {
				send_err_needmoreparams(client, cmd);
				return;
			}
			switch (do_join(client, params[0])) {
			case CHAN_INVALID_NAME:
				send_err_nosuchchannel(client, params[0]);
				break;
			case CHAN_NO_MEM:
				fprintf(
					stderr,
					"::interpretmsg.c:interpret_msg(): No memory to allocate new channel, ignoring request.\n");
				break;
			}
		}
		if (strcasecmp(cmd, ==, "part")) {
			/*
			        ERR_NEEDMOREPARAMS
			        ERR_NOTONCHANNEL
			        ERR_NOSUCHCHANNEL
			 */
			if (params_size < 1) {
				send_err_needmoreparams(client, cmd);
				return;
			}
			switch (do_part(client, params[0], params_size > 1 ? params[1] : client->nick)) {
			case CHAN_INVALID_NAME:
				send_err_nosuchchannel(client, params[0]);
				break;
			case CHAN_NOT_ON_CHANNEL:
				send_err_notonchannel(client, params[0]);
				break;
			}
		}
		/* ... */
		return;
	}
}
