#ifndef __PARSEMSG_GUARD__
#define __PARSEMSG_GUARD__
#include "protocol.h"
#include "client.h"

/** @file
	@brief IRC Messages parser
	
	Functions that are used to parse an IRC message.
	See RFC section 2.3 to learn about IRC messages syntax.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Flag used to indicate that a carriage return has been read on an IRC message. */
#define STATUS_SEEN_CR 0x1

/** Flag used to indicate that a line feed has been ead on an IRC message */
#define STATUS_SEEN_LF 0x2

/** Return value for `next_msg()` when a message parsing was stopped because no more information is available to keep on going, and we must wait for new data to arrive on the socket. 
	For example, we can read "NICK example\\r\\nUSER someone 0", where the NICK command is complete, but no terminating sequence was read for USER yet. In this case, first we would
	report a new command - NICK - and in the next call, we would report `MSG_CONTINUE`, since we have to wait for more information to come into the socket to finish parsing USER.
*/
#define MSG_CONTINUE -2

/** Return value for `next_msg()` when a message has been read that can't possibly be a well formed message. This happens when it is possible to traverse the whole buffer message without
	reading any IRC message terminators.
*/
#define MSG_FINISH_ERR -1

/** This structure represents the status of a socket read. Socket reading is undeterministic by nature: multiple messages can arrive in a single read, or no complete messages may arrive.
	Note that even though we're using TCP connections, the sockets implementation uses buffers, and will not always deliver data exactly as it was sent in the other end.
	For example, a client may write "NICK <nick>\\r\\n" and then "USER <user> 0 * :GECOS field\\r\\n", but in the server side, when we read, we get the whole thing with one read.
	The opposite is also true: a client can send "NICK <nick>\\r\\n", and we may read "NI", then "CK ", then "<ni", etc, you get the point.
	Even if sockets didn't behave this way, we need this structure to be able to read multiple messages that arrive at once: there is nothing in the RFC stopping the client from sending
	a sequence with multiple commands, and it is often the case that IRC clients send NICK, PASS and USER commands at the same time.
	Note that any message can be arbitrarily fragmented, including in between the terminating characters \\r\\n.
	Thus, this structure allows `read_data()` and `next_msg()` to guide themselves and keep track of what has been read so far. These routines provide a service to anyone using them: they
	make it look like only one single, full IRC messages arrives to the socket at each time.
*/	
struct irc_message {
	char msg[MAX_MSG_SIZE]; /**<IRC message buffer, holding up to `MAX_MSG_SIZE` characters, including the terminating characters \\r\\n. This buffer is not null terminated. */
	int status; /**<A bits flag used to keep track of which terminating sequences have been read on the current message. `STATUS_SEEN_CR` indicates we have seen \\r, and `STATUS_SEEN_LF indicates we read \\n. */
	int index; /**<This field denotes the next free position in `msg`. Any new data arriving on the socket shall be written starting at `msg[index]`. `index` is always lower than `MAX_MSG_SIZE`. */
	int last_stop; /**<Used to keep track of where we previously stopped processing the current message. This field allows `next_msg()` to resume parsing for the rest of the information not yet parsed but already retrieved from the socket. */
	int msg_begin; /**<Index that denotes the position in `msg` where the current message begins. There can be old messages behind which were already reported. Anything behind `msg_begin` is trash. */
};

/* Documented in parsemsg.c */
void initialize_irc_message(struct irc_message *in);
void read_data(struct irc_client *client);
int parse_msg(char *buf, char **prefix, char **cmd, char *params[MAX_IRC_PARAMS], int *params_filled);
int next_msg(struct irc_message *client_msg, char **msg);

#endif /* __PARSEMSG_GUARD__ */
