#include <pthread.h>
#include <limits.h>
#include <ev.h>
#include "trie.h"
#include "list.h"
#include "client_list.h"
#include "channel.h"
#include "send_rpl.h"
#include "write_msgs_queue.h"
#include "serverinfo.h"
#include "msgio.h"
#include "wrappers.h"

/** @file
   @brief Channels management module
   This file defines a set of functions used to manage channel commands other than PRIVMSG, such as joins, parts, kicks,
      modes, etc.
   For PRIVMSG, since the syntax is basically the same as private messages between 2 users, we delegate this work to
      `notify_privmsg()`.
   A thread-safe channel list is kept. With the exception of `chan_init()` and `chan_destroy()`, it is safe to call
      every other public function concurrently.
   Note that `static` functions, that is, internal functions only used in this file, are NOT thread-safe, since it is
      assumed they are invoked from within the other public,thread-safe functions.
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
 */

/** A constant denoting the number of different characters allowed in a channel name.
   Since only 7 characters are not allowed, we chose to map every character and include thisrule in `is_valid()`.
 */
#define CHANNEL_ALPHABET_SIZE (UCHAR_MAX + 1)

/** This structure represents a channel user. We will store instances of this structure associated to each nick in the
   channel in a trie */
struct chan_user {
	unsigned modes; /**<This user's status in the channel */
	struct irc_client *user; /**<Pointer to this user's client structure */
};

/** This structure represents an IRC channel
   The IRCd keeps a thread-safe list of channels associating each channel name to a node holding this structure.
 */
struct irc_channel {
	char *name; /**<Null terminated characters sequence holding the channel name */
	char *topic; /**<Channel topic */
	struct trie_t *users; /**<List of users on this channel */
	int users_count; /**<How many users are in the channel */
	unsigned modes; /**<Channel modes */
};

/** An arguments wrapper for `list_find_and_execute()` functions. */
struct irc_channel_wrapper {
	struct irc_client *client; /**<Original client where the request came from */
	char *channel; /**<Channel name */
	char *msg; /**<Message to send to the channel users, if it was a PRIVMSG command */
	char irc_reply[MAX_MSG_SIZE+1]; /**<Complete IRC Message to send to other channel users. This is used because we only need to print the message
										once into the buffer, and then echo it to every other channel user. Thus, this can be a join message, part, quit,
										privmsg, etc. This buffer must be null terminated. */
};

/**Global channels list for the whole network */
Word_list_ptr channels;

/** Defines valid characters for a channel name. As of this writing, the protocol allows any character except NUL, BELL,
   CR, LF, SPACE, COMMA and SEMI-COLLON.
   @param c The character to analyze
   @return `1` if `c` is allowed in a channel name; `0` otherwise.
 */
static int is_valid(char c)
{
	return c != '\0' && c != '\a' && c != '\r' && c != '\n' && c != ' ' && c != ',' && c != ':';
}

/** Maps a character ID into the corresponding character. Since we allow nearly any character, this is a direct mapping
   @param i The character ID
   @return Corresponding character whose ID is `i`
 */
static char pos_to_char(int i)
{
	return (char)i;
}

/** Maps a character into a unique ID. Since we allow nearly any character, this is a direct mapping
   @param c The character
   @return `c`'s unique ID.
 */
static int char_to_pos(char c)
{
	return (int)(unsigned char)c;
}

/** Initializes the channels module.
   @return `0` on success; `-1` on failure. `-1` indicates a resources allocation error.
   @warning This function must be called exactly once, by the parent thread, before any thread is created and tries to
      access any channel.
 */
int chan_init(void)
{
	if ((channels = init_word_list(NULL, is_valid, pos_to_char, char_to_pos, CHANNEL_ALPHABET_SIZE)) == NULL) {
		return -1;
	}
	return 0;
}

/** Destroys every channel.
   @warning This function must be called exactly once, by the parent thread, after every thread is dead and no more
      accesses to the channels will be performed.
 */
void chan_destroy(void)
{
	destroy_word_list(channels, LIST_NO_FREE_NODE_DATA);
}

/** Notifies a user in a channel with a generic complete IRC message passed through `args`. 
	To do so, it enqueues a new IRC message into `to_notify`'s messages queue and sends a libev async signal to his thread.
	This function is used by `JOIN`, `QUIT`, `PART`, `PRIVMSG`, and other channel commands that must be propagated to every user
	in a channel.
	@param to_notify_generic A pointer to a client structure denoting the client to notify. This client is inside the channel.
	@param args A `struct irc_channel_wrapper *` holding a valid null terminated characters sequence in the field `irc_reply`.
				This sequence will be enqueued to `to_notify_generic`'s messages write queue, thus, it must be a
				valid IRC message.
 */
static void notify_channel_user(void *to_notify_generic, void *args) {
	struct irc_client *to_notify = ((struct chan_user *) to_notify_generic)->user;
	struct irc_channel_wrapper *info = (struct irc_channel_wrapper *) args;
	client_enqueue(&to_notify->write_queue, info->irc_reply);
	ev_async_send(to_notify->ev_loop, &to_notify->async_watcher);
}

/** Auxiliary function indirectly used by `join_ack()` that is called for every user inside a channel after a new user
   joins and is added to the channel's userlist.
   For each user inside a channel, an `RPL_NAMREPLY` message is sent to the new user informing him of who is inside the
      channel at the moment, as specified by the RFC.
   This list contains the user himself.
   For every other client, a join notification is also sent by calling `notify_channel_user()`.
   @param chanuser A pointer to `struct chan_user` denoting the current user in this iteration. `chanuser` is always
      casted to `struct chan_user `.
   @param args A pointer to `struct irc_channel_wrappers` that shall contain the client where the join request
      originated from, and the target channel name. This parameter is always casted to `struct irc_channel_wrapper `.
 */
static void join_ack_aux(void *chanuser, void *args)
{
	char msg[MAX_MSG_SIZE + 1];
	int size;
	struct chan_user *chanusr;
	struct irc_channel_wrapper *info;

	chanusr = (struct chan_user*)chanuser;
	info = (struct irc_channel_wrapper*)args;
	size = cmd_print_reply(msg, sizeof(msg),
			       ":%s " RPL_NAMREPLY " %s = %s :%s!%s@%s\r\n",
			       get_server_name(), info->client->nick, info->channel, chanusr->user->nick,
			       chanusr->user->username,
			       chanusr->user->public_host);
	(void)write_to(info->client, msg, size);
	if (chanusr->user != info->client) {
		notify_channel_user(chanusr, args);
	}
}

/** Acknowledges a JOIN command issued by `client` to join channel `chan`.
   Sends a JOIN reply to the requester, followed by `RPL_TOPIC`, and then iterates through every client in a channel to
      generate the appropriate `RPL_NAMREPLY` entries.
   While doing so, it also notifies other clients about this new client.
   @param client Pointer to a client's structure denoting the client who issued the JOIN command.
   @param chan A pointer to the channel instance where `client` wants to join.
 */
static void join_ack(struct irc_client *client, irc_channel_ptr chan)
{
	char msg[MAX_MSG_SIZE + 1];
	int size;
	struct irc_channel_wrapper args;

	size = cmd_print_reply(msg,
			       sizeof(msg),
			       ":%s!%s@%s JOIN :%s\r\n",
			       client->nick,
			       client->username,
			       client->public_host,
			       chan->name);
	(void)write_to(client, msg, size);
	size = cmd_print_reply(msg, sizeof(msg),
			       ":%s MODE %s +nt\r\n", get_server_name(), chan->name);
	(void)write_to(client, msg, size);
	size = cmd_print_reply(msg, sizeof(msg),
			       ":%s " RPL_TOPIC " %s %s :%s\r\n",
			       get_server_name(), client->nick, chan->name, chan->topic);
	(void)write_to(client, msg, size);

	args.client = client;
	args.channel = chan->name;
	cmd_print_reply(args.irc_reply, sizeof(args.irc_reply), ":%s!%s@%s JOIN %s\r\n", client->nick, client->username, client->public_host, chan->name);
	trie_for_each(chan->users, join_ack_aux, (void*)&args);
	size = cmd_print_reply(msg, sizeof(msg),
			       ":%s " RPL_ENDOFNAMES " %s %s :End of NAMES list\r\n",
			       get_server_name(), client->nick, chan->name);
	(void)write_to(client, msg, size);
}

/** Called every time a client joins a nonexisting chan, thus creating it implicitly.
   This function will allocate and store a new channel structure in the server's channels list, and add the requesting
      clientto the channel's user list. Then, the join request is acknowledged using `join_ack()`.
   @param args A pointer to `struct irc_channel_wrapper` holding information about the channel name and the client who
      issued the command.
   This parameter is always casted to `struct irc_channel_wrapper `.
   @return `NULL` if it was not possible to create a new channel name due to lack of memory.
   Otherwise, this function will return an `irc_channel_ptr` for the newly created channel casted to `void `.
 */
static void *join_newchan(void *args)
{
	struct irc_channel_wrapper *info;
	irc_channel_ptr new_chan;
	struct chan_user *new_user;

	info = (struct irc_channel_wrapper*)args;
	if ((new_chan = malloc(sizeof(*new_chan))) == NULL || (new_user = malloc(sizeof(*new_user))) == NULL) {
		return NULL;
	}
	if ((new_chan->name = strdup(info->channel)) == NULL) {
		free(new_chan);
		free(new_user);
		return NULL;
	}
	if ((new_chan->users =
		     init_trie(NULL, nick_is_valid, nick_pos_to_char, nick_char_to_pos, NICK_EDGES_NO)) == NULL) {
		free(new_chan->name);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	if (list_add_nolock(channels, new_chan, info->channel) == LST_NO_MEM) {
		destroy_trie(new_chan->users, TRIE_NO_FREE_DATA, NULL);
		free(new_chan->name);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	new_user->modes = 0;
	new_user->user = info->client;
	if (add_word_trie(new_chan->users, info->client->nick, (void*)new_user) == TRIE_NO_MEM) {
		free(new_chan->name);
		list_delete_nolock(channels, info->channel);
		destroy_trie(new_chan->users, TRIE_NO_FREE_DATA, NULL);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	new_chan->users_count = 1;
	new_chan->modes = 0;

	new_chan->topic = "No topic. yaIRCd doesn't support TOPIC command yet!";
	join_ack(info->client, new_chan);
	return (void*)new_chan;
}

/** Called every time a client joins an existing chan.
   This function creates a new `chan_user` instance, adds it to the channel, increments the channel users count, and
      acknowledges the join request using `join_ack()`.
   @param channel An `irc_channel_ptr` holding information about the channel. This parameter is always casted to
      `irc_channel_ptr`.
   @param args A pointer to `struct irc_channel_wrapper` holding the original client where the request came from. This
      parameter is always casted to `struct irc_channel_wrapper `.
   @return `NULL` if it was not possible to join this user due to lack of memory, in which case no `join_ack()` is
      performed.
   Otherwise, this function will return a pointer to `struct chan_user` holding the new channel user instance.
 */
static void *join_existingchan(void *channel, void *args)
{
	struct irc_channel_wrapper *info;
	struct chan_user *new_user;
	irc_channel_ptr chan;

	info = (struct irc_channel_wrapper*)args;
	chan = (irc_channel_ptr)channel;
	if ((new_user = malloc(sizeof(*new_user))) == NULL) {
		return NULL;
	}
	new_user->modes = 0;
	new_user->user = info->client;
	if (add_word_trie(chan->users, info->client->nick, (void*)new_user) == TRIE_NO_MEM) {
		free(new_user);
		return NULL;
	}
	join_ack(info->client, chan);
	chan->users_count++;
	return (void*)new_user;
}

/** Atomically handles a join command. Calls either `join_existingchan()` or `join_newchan()` while holding a global
   lock to the channels list.
   On success, acknowledges the join request and notifies every other client in the channel about the new comer.
   @param client Pointer to the client who issued the JOIN command.
   @param channel Pointer to a null terminated characters sequence holding the channel name in the JOIN command.
   @return 
	<ul>
		<li>`0` on success</li>
		<li>`CHAN_NO_MEM` if the request could not be fulfilled due to lack of memory resources</li>
		<li>`CHAN_LIMIT_EXCEEDED` if the client cannot join channels due to the maximum channel limit imposed in yaircd.conf</li>
	</ul>
 */
int do_join(struct irc_client *client, char *channel)
{
	/* TODO - Check if channel name is valid */
	struct irc_channel_wrapper args;
	void *ret;
	int result;
	int i;
	/* We don't need a mutex in client->channels_count, since only the client's thread will be accessing this field */
	if (client->channels_count == get_chanlimit()) {
		return CHAN_LIMIT_EXCEEDED;
	}
	
	args.client = client;
	args.channel = channel;
	ret = list_find_and_execute_globalock(channels,
					      channel,
					      join_existingchan,
					      join_newchan,
					      (void*)&args,
					      (void*)&args,
					      &result);
	if (ret == NULL) {
		return CHAN_NO_MEM;
	}
	else {
		/* assert: client->channels_count < get_chanlimit() => exists i in [0, ..., get_chanlimit()-1] s.t. client->channels[i] == NULL */
		for (i = 0; client->channels[i] != NULL; i++)
			; /* Intentionally left blank */
		/* assert: client->channels[i] == NULL */
		if ((client->channels[i] = strdup(channel)) == NULL) {
			do_part(client, channel, client->nick);
			return CHAN_NO_MEM;
		}
		client->channels_count++;
	}
	return 0;
}

/** Destroys a channel when it no longer holds any client, freeing every allocated resources. This is called by
   `leave_channel()` when the last client leaves the channel.
   @param chan An `irc_channel_ptr` holding the channel to destroy.
 */
static void destroy_channel(irc_channel_ptr chan)
{
	(void)list_delete_nolock(channels, chan->name);
	free(chan->name);
	/*free(chan->topic);*/
	destroy_trie(chan->users, TRIE_NO_FREE_DATA, NULL);
	free(chan);
}

/** Callback function used by `do_quit()` and `do_part()` to process the event triggered for a client leaving a
	channel.
	This function will delete the client from the channel's user list, and notify every other channel user about this.
	If the channel becomes empty as a result of this user leaving, `destroy_channel()` is called.
	@param channel An `irc_channel_ptr` holding the target channel.
	@param args A `struct irc_channel_wrapper *` which must hold a valid pointer to the client leaving in the
				`client` field.
	@return `NULL` if the user was not on the channel, `args` otherwise.
*/	
static void *leave_channel(void *channel, void *args)
{
	irc_channel_ptr chan;
	struct irc_channel_wrapper *info;
	void *user;

	info = (struct irc_channel_wrapper*)args;
	chan = (irc_channel_ptr)channel;
	if ((user = delete_word_trie(chan->users, info->client->nick)) == NULL) {
		return NULL;
	}
	free(user);
	trie_for_each(((irc_channel_ptr)channel)->users, notify_channel_user, args);
	if (--chan->users_count == 0) {
		/* Channel empty, clear up */
		destroy_channel(chan);
	}
	return args;
}

/** This is the function invoked by the rest of the code to deal with QUIT messages.
   It indirectly invokes `leave_channel()` using `list_find_and_execute_globalock()`.
   The following actions are performed for each channel in the user's channels list:
	<ul>
		<li>The user is deleted from the channel's user list</li>
		<li>Other channel users are notified about this</li>
		<li>If the channel becomes empty, it is deleted</li>
		<li>Finally, the channel name is removed from this user's channels list</li>
	</ul>
   @param client The client where the QUIT request came from.
   @param quit_msg Quit message. The code using this function should always provide a quit message.
   This must be a valid null terminated characters sequence.
 */
void do_quit(struct irc_client *client, char *quit_msg) {
	struct irc_channel_wrapper args;
	int result;
	int i;
	args.client = client;
	cmd_print_reply(args.irc_reply, sizeof(args.irc_reply), ":%s!%s@%s QUIT :%s\r\n", client->nick, client->username, client->public_host, quit_msg);
	for (i = 0; i < get_chanlimit(); i++) {
		if (client->channels[i] != NULL) {
			(void) list_find_and_execute_globalock(channels, client->channels[i], leave_channel, NULL, (void*)&args, NULL, &result);
			free(client->channels[i]);
		}
	}
	client->channels_count = 0;
}

/** This is the function invoked by the rest of the code to deal with PART messages.
   It indirectly invokes `leave_channel()` using `list_find_and_execute_globalock()`. Note that `leave_channel()` is only
      called if the channel really exists.
   When the channel exists, the user is deleted from the channel's user list, the channel name is removed from this user's channels list,
   other channel users are notified about this, and finally, if the channel becomes empty, it is deleted.
   @param client The client where the PART request came from.
   @param channel Channel name.
   @param part_msg Part message. The code using this function should always provide a part message. If the client didn't
      provide one, the default, as defined by the protocol, is to use the client's nickname.
   This must be a valid null terminated characters sequence.
   @return `0` on success; `CHAN_NOT_ON_CHANNEL` if the client tried to part a channel he's not part of, in which case
      nothing happens.
 */
int do_part(struct irc_client *client, char *channel, char *part_msg)
{
	/* TODO - Check if channel name is valid */
	int i;
	struct irc_channel_wrapper args;
	int result;
	void *ret;
	args.client = client;
	args.channel = channel;
	
	cmd_print_reply(args.irc_reply, sizeof(args.irc_reply), ":%s!%s@%s PART %s :%s\r\n", client->nick, client->username, client->public_host, channel, part_msg);
	
	ret = list_find_and_execute_globalock(channels, channel, leave_channel, NULL, (void*)&args, NULL, &result);
	if (result != 0 && ret == NULL) {
		/* Attempted to part a channel he's not part of */
		return CHAN_NOT_ON_CHANNEL;
	}
	else {
		for (i = 0; 
			 i < get_chanlimit() && (client->channels[i] == NULL ? 1 : strcasecmp(client->channels[i], !=, channel));
			 i++)
			; /* Intentionally left blank */
		/* assert: i >= get_chanlimit() || strcasecmp(client->channels[i], ==, channel) */
		if (i >= get_chanlimit()) {
			/* This should never happen if we got past the test result != 0 && ret == NULL */
			fprintf(stderr, "::channel.c:do_part(): User successfully parted from channel, but there's no such entry in client->channels\n");
			return 0; /* Do we really want to return 0? Well, the part was successfull... */
		}
		else {
			free(client->channels[i]);
			client->channels[i] = NULL;
			client->channels_count--;
		}
	}
	return 0;
}

/** Callback function invoked for each user in a channel. It delivers a new channel message to a user who is part of
   that channel.
   @param channel_user A `struct chan_user ` describing this user.
   @param args A `struct irc_channel_wrapper ` holding the message's author, the message, and the target channel as a
      characters sequence.
 */
static void send_msg_to_chan_aux(void *channel_user, void *args)
{
	if (((struct chan_user*)channel_user)->user != ((struct irc_channel_wrapper*)args)->client) {
		notify_channel_user(channel_user, args);
	}
}

/** Iterates through every user in a channel and delivers a new message. This is called by `channel_msg()` using
   `list_find_and_execute()`.
   @param channel An `irc_channel_ptr` holding the target channel information.
   @param arg A `struct irc_channel_wrapper ` holding the client who sent a message, the channel name, and the message.
   @return This function always returns `NULL`.
 */
static void *send_msg_to_chan(void *channel, void *arg)
{
	trie_for_each(((irc_channel_ptr)channel)->users, send_msg_to_chan_aux, arg);
	return NULL;
}

/** Function responsible for dealing with channel PRIVMSG command. This is the function invoked by the rest of the code.
   It indirectly invokes `send_msg_to_chan()` using `list_find_and_execute()`. On success, the message is delivered to
      every other client on the channel.
   @param from The message's author.
   @param channel Target channel.
   @param msg The message.
   @return `0` on success; `CHAN_NO_SUCH_CHANNEL` if there isn't a channel named `channel`.
 */
int channel_msg(struct irc_client *from, char *channel, char *msg)
{
	/* TODO Check if client is really on channel */
	struct irc_channel_wrapper args;
	int result;
	args.client = from;
	args.channel = channel;
	cmd_print_reply(args.irc_reply, sizeof(args.irc_reply), ":%s!%s@%s PRIVMSG %s :%s\r\n", from->nick, from->username, from->public_host, channel, msg);
	list_find_and_execute(channels, channel, send_msg_to_chan, NULL, (void *) &args, NULL, &result);
	if (result == 0) {
		return CHAN_NO_SUCH_CHANNEL;
	}
	return 0;
}

/**
 * This function sends to the client a line denoting information about the channel, in response to a LIST command.
 * @param data The channel data
 * @param args The client who invoked the LIST command.
 */
static void list_channel(void *data, void *args)
{
	char msg[MAX_MSG_SIZE + 1];
	int size;
	irc_channel_ptr channel;
	struct irc_client * client;
	
	channel = (irc_channel_ptr)data;
	client = (struct irc_client*)args;

	size = cmd_print_reply(msg, sizeof(msg),
					":%s " RPL_LIST " %s %s %d :%s\r\n",
					get_server_name(), 
					client->nick, 
					channel->name, 
					channel->users_count, 
					channel->topic);
					
	(void)write_to(client, msg, size);
}

/**
 * This function lists every channel available and a line denoting the end of the list of the channels, in response to a LIST command.
 * @param client The client who invoked the LIST command.
 */
void list_each_channel(struct irc_client *client)
{
	char msg[MAX_MSG_SIZE + 1];
	int size;
	
	list_for_each(channels, list_channel, client);
	
	size = cmd_print_reply(msg, sizeof(msg),
				":%s " RPL_LISTEND " %s :End of LIST\r\n",
				get_server_name(), client->nick);
				
	(void)write_to(client, msg, size);
}
