#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "protocol.h"
#include "interpretmsg.h"
#include "wrappers.h"
#include "client_list.h"
#include "client.h"
#include "channel.h"
#include "serverinfo.h"
#include "trie.h"
#include "send_err.h"
#include "send_rpl.h"
#include "msgio.h"

/** @file
   @brief Functions responsible for interpreting an IRC message.
   This file implements functions that are responsible for interpreting an IRC message.
   The goal is to provide an abstract function that shall be called everytime a new message arrived from a client. That
   function will interpret the message, decide if it's valid, and generate the appropriate command reply, or error
   reply if it was an invalid message.
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
   @see parsemsg.c
 */

/** A macro to count the number of elements in an array. Remember that arrays decay into pointers in an expression; as a
   consequence, when passed to other functions, arrays are seen as pointers and this macro will wrongly expand to
   `sizeof(&a[0])/sizeof(a[0])`, i.e., `sizeof(a)` will be interpreted as the size of an element of type 
   `(typeof(a[0]) *)` 
*/
#define array_count(a) (sizeof(a) / sizeof(*a))

/** A trie used to store function pointers to process every commands issued by registered connections */
static struct trie_t *commands_registered;
/** A trie used to store function pointers to process every commands issued by unregistered connections */
static struct trie_t *commands_unregistered;

/** This structure defines a trie commands' node. It contains the command itself, i.e., the path taken by the trie to
   arrive at this node, and a pointer to a function that knows how to process this command. */
struct cmd_func {
	char *command; /**<A null-terminated characters sequence describing the command that `f` knows how to process */
	/** A pointer to a function that shall process `command`. `client` will be the client where the request came
	   from. The rest of the parameters correspond to the values filled by `parsemsg()`. */
	void (*f)(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
};

/* We don't want to offer command processing functions as an interface to the rest of the code, hence, we make forward
   declarations in here, not in the header file. We do so to keep cmds_unregistered and cmds_registered arrays in the
      top
   of this file. These functions are documented below. */
void cmd_nick_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_user_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_nick_registered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_user_registered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_quit(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_privmsg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_whois(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_join(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_part(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_list(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
void cmd_pong(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);

/** The core processing functions. This array holds as many `struct cmd_func` instances as the number of commands
   available for unregistered connections. Developers adding new commands to yaIRCd for unregistered users only need to
   change this array by adding one more element with the new command and the function to be called when someone issues
   that command.
 */
static const struct cmd_func cmds_unregistered[] = {
	{ "nick", cmd_nick_unregistered },
	{ "user", cmd_user_unregistered },
	{ "pong", cmd_pong }
};

/** This array holds the commands for registered connections. Each entry is an instance of `struct cmd_func`, thus, this
   array can be seen as holding a pair of `(command, function_ptr)` in each entry. Developers adding new commands for
   registered connections will only need to implement the function to process the new command, and add a new entry in
   this array.
 */
static const struct cmd_func cmds_registered[] = {
	{ "nick", cmd_nick_registered },
	{ "user", cmd_user_registered },
	{ "quit", cmd_quit },
	{ "privmsg", cmd_privmsg },
	{ "whois", cmd_whois },
	{ "join", cmd_join },
	{ "part", cmd_part },
	{ "list", cmd_list },
	{ "pong", cmd_pong }
};

/** Processes a `NICK` command for an unregistered connection.
	This function makes use of the atomic `client_list_add()` operation.
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NONICKNAMEGIVEN` if `params_size` is clearly not enough such that a nick is in `params[0]`.</li>
	<li>`ERR_ERRONEUSNICKNAME` if the provided nickname exceeds `MAX_NICK_LENGTH`, or the nick contains invalid
	   characters, which is reported by `client_list_add()`</li>
	<li>`ERR_NICKNAMEINUSE` if there's already a client, possibly unregistered, who chose this nickname</li>
	</ul>
	If no error condition occurs and the client already chose username, realname and GECOS, the client's request is
	   acknowledged with the welcome message (see `send_welcome()`), along with the MOTD. If no error occurs, but
	   the client has not yet defined realname, username and GECOS, no reply is given.
	If there's no memory to store the new nickname, `pthread_exit()` is called, and the client's connection is
	   closed.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`ERR_NICKCOLLISION` (description: "&lt;nick&gt; :Nickname collision KILL from &lt;user&gt;@&lt;host&gt;") -
	   returned by a server to a client when it detects a nickname collision (registered of a NICK that already
	   exists by another server). Since we do not have remote servers, this error is never reported to a client.
	   </li>
	<li>`ERR_UNAVAILRESOURCE` (description: "&lt;nick/channel&gt; :Nick/channel is temporarily unavailable") -
	   returned by a server to a user trying to join a channel currently blocked by the channel delay mechanism.
	   Returned by a server to a user trying to change nickname when the desired nickname is blocked by the nick
	   delay mechanism. Since we do not implement delay mechanisms yet, these errors are never reported.</li>
	<li>`ERR_RESTRICTED` (description: ":Your connection is restricted!") - sent by the server to a user upon
	   connection to indicate the restricted nature of the connection (user mode "+r"). We do not have user modes
	   implemented, hence, this error is never reported.</li>
	</ul>
 */
void cmd_nick_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
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
		terminate_session(client, NO_MEM_QUIT_MSG);
		return;
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
		terminate_session(client, NO_MEM_QUIT_MSG);
		return;
	}
	if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
		client->is_registered = 1;
		send_welcome(client);
		send_motd(client);
	}
}

/** Processes a `USER` command for an unregistered connection.
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NEEDMOREPARAMS` if `params_size` is clearly not enough such that username, host, server, and GECOS are
	   stored in `params`.</li>
	</ul>
	If no error condition occurs and the client already defined a nickname, the client's request is acknowledged
	   with the welcome message (see `send_welcome()`), along with the MOTD. If no error occurs and no nickname has
	   been chosen yet, no reply is generated.
	If there's no memory to store the new information, `pthread_exit()` is called, and the client's connection is
	   closed.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
 */
void cmd_user_unregistered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
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
		terminate_session(client, NO_MEM_QUIT_MSG);
		return;
	}
	if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
		client->is_registered = 1;
		send_welcome(client);
		send_motd(client);
	}
}

/** Processes a `PONG` command, resulting from a previous PING request issued by the server.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>
		`ERR_NOSUCHSERVER` (description: "&lt;server name&gt; :No such server") - Used to indicate the server name given currently does not exist. Since we do not have remote servers, this error is never reported to a client.
	</li>
	</ul>
*/
void cmd_pong(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size) { 
	if (params_size < 1) {
		send_err_noorigin(client);
	}
}

void cmd_nick_registered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	/* TODO implement NICK for registered users */
}

/** Processes a `USER` command for a registered connection.
	It is an error to attempt using `USER` command in a registered connection. Thus, upon calling this function, the
	   client is notified with the error `ERR_ALREADYREGISTRED`.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
 */
void cmd_user_registered(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	send_err_alreadyregistred(client);
}

/** Processes a `QUIT` command for a registered connection.
	QUIT commands are processed like any other commands. This function will go through the parameteres sequence,
	as returned by `parse_msg()`, to see if a quit message was provided. If no quit message is provided,
	`DEFAULT_QUIT_MSG`, as defined in `protocol.h`, is assumed.
	If a quit message is provided, it is copied to a temporary buffer and prefixed with `QUIT_MSG_PREFIX`,
	for the reasons explained in `protocol.h`.
	This function uses `terminate_session()` to actually close the connection.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
*/
void cmd_quit(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	char quit_msg[MAX_QUITMSG_LENGTH];
	char *msg;
	
	if (params_size >= 1) {
		strcpy(quit_msg, QUIT_MSG_PREFIX);
		strncat(quit_msg, params[0], sizeof(quit_msg)-sizeof(QUIT_MSG_PREFIX)-2);
		msg = quit_msg;
	}
	else {
		msg = DEFAULT_QUIT_MSG;
	}
	terminate_session(client, msg);
}

/** Callback function used by `cmd_privmsg()` when a PRIVMSG command is issued on a one-to-one private conversation. The
   clients list implementation will call this function while holding a lock to the target client list node.
        @param target_client A `(struct irc_client *)` holding the target client's informations.
        @param args A wrapper holding informations returned by `parse_msg()` packed in a pointer to `struct cmd_parse`.
 */
static void *cmd_privmsg_aux(void *target_client, void *args)
{
	struct cmd_parse *info = (struct cmd_parse*)args;
	struct irc_client *target = (struct irc_client*)target_client;
	notify_privmsg(info->from, target, target->nick, info->params[1]);
	return NULL;
}

/** Processes a generic `PRIVMSG` command. `PRIVMSG` can arise on a one-to-one private conversation, or on a one-to-many
conversation (channel message).
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NORECIPIENT` if `params_size` is clearly not enough such that the message's recipient is in
	   `params`.</li>
	<li>`ERR_NOTEXTTOSEND` if `params_size` cannot possibly be a value such that both a recipient and a message are
	   stored in `params`.</li>
	<li>`ERR_NOSUCHNICK` if the message's target does not exist.</li>
	</ul>
	If no error condition occurs, the message's target is evaluted to decide whether it is a channel message or a
	   private message for another user (no other form of PRIVMSG has been implemented). If it's a channel message,
	   `channel_msg()` is called. If it's a private message, `client_list_find_and_execute()` is called with
	   `cmd_privmsg_aux()` as a callback function.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`ERR_CANNOTSENDTOCHAN` (description: "&lt;channel name&gt; :Cannot send to channel") - Sent to a user who is
	   either (a) not on a channel which is mode +n or (b) not a chanop (or mode +v) on a channel which has mode +m
	   set or where the user is banned and is trying to send a PRIVMSG message to that channel. This has not been
	   implemented because we don't have chanops (or mode +v), and we don't have channel modes other than +n and +t.
	   We also don't have channel bans. Thus, it makes sense to implement channel functionality first before using
	   this error code.</li>
	<li>`ERR_NOTOPLEVEL` (description: "&lt;mask&gt; :No toplevel domain specified") - ERR_NOTOPLEVEL and
	   ERR_WILDTOPLEVEL are errors that are returned when an invalid use of "PRIVMSG $&lt;server&gt;" or "PRIVMSG
#&lt;host&gt;" is attempted. We don't support any of these special uses, so we do not use this error
	   code.</li>
	<li>`ERR_WILDTOPLEVEL` (description: "&lt;mask&gt; :Wildcard in toplevel domain") - ERR_NOTOPLEVEL and
	   ERR_WILDTOPLEVEL are errors that are returned when an invalid use of "PRIVMSG $&lt;server&gt;" or "PRIVMSG
#&lt;host&gt;" is attempted. We don't support any of these special uses, so we do not use this error
	   code.</li>
	<li>`ERR_TOOMANYTARGETS` (description: "&lt;target&gt; :&lt;error code&gt; recipients. &lt;abort message&gt;") -
	   Returned to a client which is attempting to send a PRIVMSG/NOTICE using the user\@host destination format and
	   for a user\@host which has several occurrences. Returned to a client which trying to send a PRIVMSG/NOTICE to
	   too many recipients. Returned to a client which is attempting to JOIN a safe channel using the shortname when
	   there are more than one such channel. We don't have safe channels, and our current PRIVMSG implementation
	   doesn't allow user\@host recipients: it only allows one nickname or one channel to be specified as the
	   message's target.</li>
	</ul>
	The following replies are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`RPL_AWAY` (description: "&lt;nick&gt, :&lt;away message&gt;") - sent to any client sending a PRIVMSG to a
	   client which is away. `RPL_AWAY` is only sent by the server to which the client is connected. We do not
	   support away status, so this reply is never generated.</li>
	</ul>
 */
void cmd_privmsg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	struct cmd_parse wrapper;
	int status;
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
	} else {
		(void)client_list_find_and_execute(params[0], cmd_privmsg_aux, (void*)&wrapper, &status);
		if (status == 0) {
			send_err_nosuchnick(client, params[0]);
		}
	}
}



/* whois auxiliar para os channels
 * DOCUMENT THIS */
static void cmd_whois_aux_channels(struct irc_client *client)
{
	int i;
	char buffer[MAX_MSG_SIZE];
	char *buf_ptr;
	char *ptr_begin;
	
	ptr_begin  = buffer;
	
	ptr_begin+=cmd_print_reply(buffer, sizeof(buffer), ":%s " RPL_WHOISCHANNELS " %s %s :", get_server_name(), client->nick, client->nick);
	buf_ptr=ptr_begin;

	for(i=0; i<get_chanlimit();i++){
		if(client->channels[i]==NULL){
			continue;
		}
		
		if((buffer+sizeof(buffer)-buf_ptr-2)<strlen(client->channels[i])+1){
			/*Didn't fit*/
			buf_ptr[0] = '\r';
			buf_ptr[1] = '\n';
			write_to(client,buffer,buf_ptr-buffer+2);
			buf_ptr=ptr_begin;
			continue;
		}
		buf_ptr+=cmd_print_reply(buf_ptr,(size_t) (buffer+sizeof(buffer)-buf_ptr), "%s ", client->channels[i]);		
	}
	if(buf_ptr != ptr_begin) {
		buf_ptr[0] = '\r';
		buf_ptr[1] = '\n';
		write_to(client,buffer,buf_ptr-buffer+2);
	}
}








/** Callback function used by `client_list_find_and_execute()` when a client issues a WHOIS command.
   This function generates and sends the appropriate WHOIS reply
   @param target_client Client being WHOIS'ed
   @param args A parameter of type `struct cmd_parse` holding information previously obtained by the messages parsing
      routine.
   @return This function always returns `NULL`.
 */
static void *cmd_whois_aux(void *target_client, void *args)
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
	/* TODO Implement RPL_WHOISIDLE */
	cmd_whois_aux_channels((struct irc_client*) target_client);
	length = cmd_print_reply(message,
				 sizeof(message),
				 ":%s " RPL_ENDOFWHOIS " %s %s :End of WHOIS list\r\n",
				 get_server_name(),
				 info->from->nick,
				 target->nick);
	(void)write_to(info->from, message, length);
	return NULL;
}

/** Processes a `WHOIS` command.
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NONICKNAMEGIVEN` if `params_size` is clearly not enough such that a nick is in `params[0]`.</li>
	<li>`ERR_NOSUCHNICK` if the target specified could not be found on the server's clients database.</li>
	</ul>
	If no error condition occurs, `client_list_find_and_execute()` is called with `cmd_whois_aux()` serving as a
	   callback function.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`ERR_NOSUCHSERVER` (description: "&lt;server name&gt; :No such server") - Used to indicate the server name
	   given currently does not exist. There is no remote servers support, it makes no sense to implement this for
	   now.</li>
	</ul>
	The following replies are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`RPL_WHOISCHANNELS` (description: "&lt;nick&gt; :*( ( "@" / "+" ) &lt;channel&gt; " " )") -
	   RPL_WHOISCHANNELS may appear more than once (for long lists of channel names). The ’@’ and ’+’ characters
	   next to the channel name indicate whether a client is a channel operator or has been granted permission to
	   speak on a moderated channel. This command can be half-implemented after adding the channels list to every
	   client.</li>
	<li>`RPL_AWAY` (description: "&lt;nick&gt, :&lt;away message&gt;") - used to indicate that this client is away.
	   `RPL_AWAY` is only sent by the server to which the client is connected. We do not support away status, so
	   this reply is never generated.</li>
	<li>`RPL_WHOISOPERATOR` (description: "&lt;nick&gt; :is an IRC operator") - Indicates IRC operator status.
	   yaIRCd doesn't support ircops, hence, not implemented.</li>
	<li>`RPL_WHOISIDLE` (description: "&lt;nick&gt; &lt;integer&gt; :seconds idle") - Indicates idle time for a
	   client. Not implemented yet, no good reason not to do so, anyone is encouraged to go ahead and implement
	   it.</li>
	</ul>
 */
void cmd_whois(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	struct cmd_parse wrapper;
	int status;
	if (params_size == 0) {
		send_err_nonicknamegiven(client);
		return;
	}
	wrapper.from = client;
	wrapper.prefix = prefix;
	wrapper.cmd = cmd;
	wrapper.params = params;
	wrapper.params_size = params_size;
	(void)client_list_find_and_execute(params[0], cmd_whois_aux, (void*)&wrapper, &status);
	if (status == 0) {
		send_err_nosuchnick(client, params[0]);
	}
}

/** Processes a `JOIN` command.
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NEEDMOREPARAMS` if `params_size` is clearly not enough such that the target channel cannot have
	   possibly been indicated</li>
	</ul>
	This function calls `do_join()` to process the command. See the documentation for that function for further
	   information.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`ERR_BANNEDFROMCHAN` (description: "&lt;channel&gt; :Cannot join channel (+b)") - Notifies a user that a ban
	   in a channel matches his public host. Channel bans are not supported yet, so we don't generate this
	   reply.</li>
	<li>`ERR_INVITEONLYCHAN` (description: "&lt;channel&gt; :Cannot join channel (+i)") - Notifies a client that a
	   channel is invite only. We do not support channel modes, so we don't generate this reply.</li>
	<li>`ERR_BADCHANNELKEY` (description: "&lt;channel&gt; :Cannot join channel (+k)") - Notifies a client that he
	   attempted to join a channel that is password protected and didn't provide a password. Channel modes are not
	   supported in yaIRCd, so we never generate this error.</li>
	<li>`ERR_CHANNELISFULL` (description: "&lt;channel&gt; :Cannot join channel (+l)") - Used in conjunction with
	   the +l mode to indicate the maximum number of allowed users for this channel has been reached. Channel modes
	   are not supported by yaIRCd, so we never generate this error.</li>
	<li>`ERR_BADCHANMASK` (description: "&lt;channel&gt; :Bad Channel Mask") - Used when malformed channel masks are
	   passed in a JOIN command. Not implemented, since yaIRCd doesn't support channel masks.</li>
	<li>`ERR_NOSUCHCHANNEL` (description: "&lt;channel name&gt; :No such channel") - Used to indicate the given
	   channel name is invalid. This must still be implemented in `do_join()`.</li>
	<li>`ERR_TOOMANYCHANNELS` (description: "&lt;channel name&gt, :You have joined too many channels") - Sent to a
	   user when they have joined the maximum number of allowed channels and they try to join another channel. This
	   will be implemented as soon as we implement channel listing in each client.</li>
	<li>`ERR_TOOMANYTARGETS` (description: "&lt;target&gt; :&lt;error code&gt; recipients. &lt;abort message&gt;") -
	   Returned to a client which is attempting to JOIN a safe channel using the shortname when there are more than
	   one such channel. There are no safe channels in yaIRCd, thus we don't use this error reply.</li>
	<li>`ERR_UNAVAILRESOURCE` (description: "&lt;nick/channel&gt; :Nick/channel is temporarily unavailable") -
	   returned by a server to a user trying to join a channel currently blocked by the channel delay mechanism.
	   Returned by a server to a user trying to change nickname when the desired nickname is blocked by the nick
	   delay mechanism. Since we do not implement delay mechanisms yet, these errors are never reported.</li>
	</ul>
	@todo Consider the case in which a client issues a JOIN command for a channel he is already part of.
 */
void cmd_join(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
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
	case CHAN_LIMIT_EXCEEDED:
		send_err_toomanychannels(client, params[0]);
		break;
	}
}

/** Processes a `PART` command.
	The client can be notified of the following errors, in which case the function returns prematurely:
	<ul>
	<li>`ERR_NEEDMOREPARAMS` if `params_size` is clearly not enough such that the target channel cannot have
	   possibly been indicated</li>
	<li>`ERR_NOTONCHANNEL` if `client` is not currently on the channel he attempted to part from.
	</ul>
	This function calls `do_part()` to process the command. If no part message was specified, the client's nickname
	   is used as the message. See the documentation for `do_part()` for further information.
	@param client The client who issued the command.
	@param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
	@param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
	@param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
	   in the IRC message arrived from `client`, as returned by `parse_msg()`.
	@param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
	@todo
	The following errors are described in the protocol as possible replies to this command, but have not been
	   implemented yet:
	<ul>
	<li>`ERR_NOSUCHCHANNEL` (description: "&lt;channel name&gt; :No such channel") - Used to indicate the given
	   channel name is invalid. This must still be implemented in `do_part()`.</li>
	</ul>
 */
void cmd_part(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
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

/**
 * Processes a 'LIST' command.
 * @param client The client who issued the command.
 * @param prefix Null terminated characters sequence holding the command's prefix, as returned by `parse_msg()`.
 * @param cmd Null terminated characters sequence holding the command itself, as returned by `parse_msg()`.
 * @param params An array of pointers to null terminated characters sequences, each one holding a parameter passed
 * in the IRC message arrived from `client`, as returned by `parse_msg()`.
 * @param params_size How many elements are stored in `params`, as returned by `parse_msg()`.
 * @todo The list command is partially implemented. Consider implementing <targets> (check RFC) by the time multiple nodes are a reality in the yaIRCd. 
 */
void cmd_list(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	list_each_channel(client);
}

/** Defines what is a valid character for a command. We only allow alphabetic characters to be part of a command.
	@param c The character to check.
	@return `0` if `c` is an invalid character, `1` otherwise.
 */
static int is_valid(char c)
{
	return isalpha((unsigned char)c);
}

/** Defines a mapping from a character ID back into its character representation. The commands trie maps a character `c`
to `c - &lsquo;a&rsquo;`, thus, to map back, we just need to return `c + &lsquo;a&rsquo;`.
	@param c Unique ID for a character
	@return A character `i` such that `char_to_pos(i) == c`
 */
static char pos_to_char(int c)
{
	return c - 'a';
}

/** Maps a character into its unique ID. We use a direct mapping in which `c` is mapped to the integer `c -
&lsquo;a&rsquo;`.
	@param c The character to map.
	@return A unique integer that represents `c`'s ID.
 */
static int char_to_pos(char c)
{
	return tolower((unsigned char)c) - 'a';
}

/** Inserts a set of commands stored in an array of commands into a given trie. This function is used by `cmds_init()`.
Its purpose is to loop through an array of `struct cmd_fund` and add each command to the specified trie.
	@param trie The trie to add the commands to.
	@param array An array of `struct cmd_fund`. Typically, this will either be `cmds_unregistered` or
	   `cmds_registered`. This array is iterated, and for each entry found, we add the pair `(array[i].command,
	   (void *) &array[i])` to the trie.
	@param array_size How many elements are stored in `array`.
	@return `-1` if `add_word_trie()` could not move forward because of some error condition, normally indicating a
	   resources allocation problem; `0` on success.
 */
static int add_commands(struct trie_t *trie, const struct cmd_func *array, size_t array_size)
{
	size_t i;
	for (i = 0; i < array_size; i++) {
		if (add_word_trie(trie, array[i].command, (void*)&array[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

/** Initializes the commands tries. There is a trie for commands issued by registered users, and a trie for command
requests coming from unregistered users. This function initializes both tries, and calls `add_commands()` to iterate
through each of the commands arrays and add each element to the appropriate trie.
	@return `0` on success; `-1` if any of the tries could not be successfully filled due to resource allocation
	   errors.
 */
int cmds_init(void)
{
	int i, j;
	if ((commands_registered = init_trie(NULL, is_valid, pos_to_char, char_to_pos, 'z' - 'a' + 1)) == NULL) {
		return -1;
	}
	if ((commands_unregistered = init_trie(NULL, is_valid, pos_to_char, char_to_pos, 'z' - 'a' + 1)) == NULL) {
		free(commands_registered);
		return -1;
	}
	i = add_commands(commands_unregistered, cmds_unregistered, array_count(cmds_unregistered));
	j = add_commands(commands_registered, cmds_registered, array_count(cmds_registered));
	return -!(i == 0 && j == 0);
}

/** Interprets an IRC message. Assumes that the message is syntactically correct, that is, `parse_msg()` did not return
   an error condition.
   Interpreting a message consists of redirecting the request to the appropriate function that knows how to process it.
      This is done by searching through the commands trie (a different trie is selected whether the request came from a
      registered or unregistered connection), and invoking the function pointed to by the trie's node data if a match is
      found.
   If a match is not found, `ERR_NOTREGISTERED` is sent if the request came from an unregistered connection;
      `ERR_UNKNOWNCOMMAND` is sent if the request came from a registered connection.
   If a match is found, i.e., the command invoked really exists, the appropriate function is called to dispatch this
      request, and is passed every argument. Thus, this specific function works for a generic set of commands, no
      changes are necessary to accomodate new commands implementations, and each specific function holds as much
      information about the request as this function, since every argument is passed to any generic function.
   @param client Pointer to the client where this message came from.
   @param prefix Pointer to a null terminated characters sequence that denotes the prefix of the message, as returned by
      `parse_msg()` [OPTIONAL].
   @param cmd Pointer to a null terminated characters sequence that denotes the command part of the message, as returned
      by `parse_msg()`.
   @param params Array of pointers to the command parameters filled by `parse_msg()`.
   @param params_size How many parameters are stored in `params`. This must be an integer greater than or equal to 0.
 */
void interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size)
{
	struct cmd_func *command_func;
	if (!client->is_registered) {
		if ((command_func = (struct cmd_func*)find_word_trie(commands_unregistered, cmd)) == NULL) {
			send_err_notregistered(client);
			return;
		}
	} else {
		if ((command_func = (struct cmd_func*)find_word_trie(commands_registered, cmd)) == NULL) {
			send_err_unknowncommand(client, cmd);
			return;
		}
	}
	(*command_func->f)(client, prefix, cmd, params, params_size);
}
