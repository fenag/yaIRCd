#ifndef __YAIRCD_CHANNEL_GUARD__
#define __YAIRCD_CHANNEL_GUARD__

/** @file
	@brief Channels management module
	
	This file declares a set of functions used to manage channel commands other than PRIVMSG, such as joins, parts, kicks, modes, etc.
	For PRIVMSG, since the syntax is basically the same as private messages between 2 users, we delegate this work to `notify_privmsg()`.
	A thread-safe channel list is kept. With the exception of `chan_init()` and `chan_destroy()`, it is safe to call every function declared in this header file concurrently.
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/

/** Returned by `do_join()` when there isn't enough memory to create a new channel */
#define CHAN_NO_MEM 1

/** Returned by `do_join()` when a user tried to create a channel with an invalid name */
#define CHAN_INVALID_NAME 2

/** Returned by `do_part()` when a user attempts to part a channel he's not in */
#define CHAN_NOT_ON_CHANNEL 3

/** Used to report when someone attempts to perform an action in a channel that does not exist */
#define CHAN_NO_SUCH_CHANNEL 4

/** Opaque type for a channelused by the rest of the code */
typedef struct irc_channel *irc_channel_ptr;

/* Documented in .c source file */
int chan_init(void);
void chan_destroy(void);
int do_join(struct irc_client *client, char *channel);
int do_part(struct irc_client *client, char *channel, char *part_msg);
int channel_msg(struct irc_client *from, char *channel, char *msg);
void list_each_channel(struct irc_client *client);

#endif /* __YAIRCD_CHANNEL_GUARD__ */
