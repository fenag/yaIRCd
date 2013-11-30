#ifndef __YAIRCD_READ_MSGS_GUARD__
#define __YAIRCD_READ_MSGS_GUARD__
#include "protocol.h"

/** @file
	@brief IRC Messages reader
	
	Functions that deal with the low-level interface aspects of socket reads and writes.
	Socket messages do not necessarily arrive at the same rate and with the same size as they were sent on the other side. For example, if a client sends the NICK command and then the USER command, we might get both of them at the same time, or we might get a piece of the first command, and then the rest of the first and the second command in another socket read.
	Functions defined in this module deal with this problem. The rest of the code is not worried about the sockets API, and it should be absolutely independent of the way messages are retrieved.
	
	The basic layout to use this module is as follows: everytime there is new data to read, `read_data()` shall be called. This function will read as many characters as it can from the socket, considering the space available in the client's messages buffer. Then, the upper layer code shall call `next_msg()` untill MSG_CONTINUE is returned, which is the code for indicating that there are no more complete IRC messages in the buffer left to process. Note that there can still be some piece of a message that has not been fully read from the socket yet; in that case, the whole cycle begins again, and the buffer is automatically flushed to read the remaining characters from the socket.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Return value for `next_msg()` when a message parsing was stopped because no more information is available to keep on going, and we must wait for new data to arrive on the socket. 
	For example, we can read "NICK example\\r\\nUSER someone 0", where the NICK command is complete, but no terminating sequence was read for USER yet. In this case, first we would
	report a new command - NICK - and in the next call, we would report `MSG_CONTINUE`, since we have to wait for more information to come into the socket to finish parsing USER.
*/
#define MSG_CONTINUE -1

/** This structure represents the status of a socket read. Socket reading is undeterministic by nature: multiple messages can arrive in a single read, or no complete messages may arrive.
	Note that even though we're using TCP connections, the sockets implementation uses buffers, and will not always deliver data exactly as it was sent in the other end.
	For example, a client may write "NICK <nick>\\r\\n" and then "USER <user> 0 * :GECOS field\\r\\n", but in the server side, when we read, we get the whole thing with one read.
	The opposite is also true: a client can send "NICK <nick>\\r\\n", and we may read "NI", then "CK ", then "<ni", etc, you get the point (although that is not very likely).
	Even if sockets didn't behave this way, we need this structure to be able to read multiple messages that arrive at once: there is nothing in the RFC stopping the client from sending
	a sequence with multiple commands, and it is often the case that IRC clients send NICK, PASS and USER commands at the same time.
	Thus, this structure allows `read_data()` and `next_msg()` to guide themselves and keep track of what has been read so far. These routines provide a service to anyone using them: they
	make it look like only one single, full IRC messages arrives to the socket at a time.
*/	
struct irc_message {
	char msg[MAX_MSG_SIZE]; /**<IRC message buffer, holding up to `MAX_MSG_SIZE` characters, including the terminating characters \\r\\n. This buffer is not null terminated. */
	int index; /**<This field denotes the next free position in `msg`. Any new data arriving on the socket shall be written starting at `msg[index]`. `index` is always lower than `MAX_MSG_SIZE`. */
	int last_stop; /**<Used to keep track of where we previously stopped processing the current message. This field allows `next_msg()` to resume parsing for the rest of the information not yet parsed but already retrieved from the socket. */
	int msg_begin; /**<Index that denotes the position in `msg` where the current message begins. There can be old messages behind which were already reported. Anything behind `msg_begin` is trash. */
};

struct irc_client;
/* Documented in read_msgs.c */
void initialize_irc_message(struct irc_message *in);
void read_data(struct irc_client *client);
int next_msg(struct irc_message *client_msg, char **msg);

#endif /* __YAIRCD_READ_MSGS_GUARD__ */
