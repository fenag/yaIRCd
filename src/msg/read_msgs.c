#include <stdio.h>
#include "read_msgs.h"
#include "client.h"
#include "msgio.h"

/** @file
	@brief IRC Messages reader
	
	Functions that deal with the low-level interface aspects of socket reads and writes.
	Socket messages do not necessarily arrive at the same rate and with the same size as they were sent on the other side. For example, if a client sends the NICK command and then the USER command, we might get both of them at the same time, or we might get a piece of the first command, and then the rest of the first and the second command in another socket read.
	Functions defined in this module deal with this problem. The rest of the code is not worried about the sockets API, and it should be absolutely independent of the way messages are retrieved.
	
	The basic layout to use this module is as follows: everytime there is new data to read, `read_data()` shall be called. This function will read as many characters as it can from the socket, considering the space available in the client's messages buffer. Then, the upper layer code shall call `next_msg()` untill MSG_CONTINUE is returned, which is the code for indicating that there are no more complete IRC messages in the buffer left to process. Note that there can still be some piece of a message that has not been fully read from the socket yet; in that case, the whole cycle begins again, and the buffer is automatically flushed to read the remaining characters from the socket.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Initializes a `struct irc_message`, typically from a client.
   @param in The structure to initialize.
 */
void initialize_irc_message(struct irc_message *in)
{
	in->index = 0;
	in->last_stop = 0;
	in->msg_begin = 0;
};

/** Copies every characters from `buf[0..length-1] to `to`. Assumes `to` has enough space, which is safe because this
   function is only used inside this fileas an auxiliary function from `next_msg()`.
   @param to Pointer to the beginning of the target buffer.
   @param buf Pointer to the buffer being copied.
   @param length How many characters to copy from `buf`.
   @note `to` and `buf` may overlap in memory, and in fact that is the case. `next_msg()` uses this function to bring a
      message to the front of the queue.
 */
static void bring_to_top(char *to, char *buf, int length)
{
	if (buf == to) {
		return;
	}
	for (; length-- > 0; *to++ = *buf++)
		;  /* Intentionally left blank */
}

/** Called everytime there is new data to read from the socket. After calling this function, it is advised to use
   `next_msg()` to retrieve the IRC messages that can be extracted from this read,otherwise, the caller risks losing
   space in the messages buffer.
   @param client The client that transmitted new data.
   @note This function never overflows. If it is called repeatedly without calling `next_msg()`, it will eventually run
      out of space and throw away everything read, emptying the buffer.
   @note This function only eads what it can. This means that, for example, there may be 256 characters available to
      read from the socket, but if there's only space to read 28, only 28 are read.
   This is generally the case when IRC messages were fragmented and we are waiting for the rest of some message, which
      means our buffer is not empty.
   The function shall be called again if it is known that there is more data in the socket to parse, but only after
      calling `next_msg()` to free some space in the buffer.
 */
void read_data(struct irc_client *client)
{
	struct irc_message *client_msg = &client->last_msg;
	if (sizeof(client_msg->msg) <= client_msg->index) {
		/* If we get here, it means we have read a characters sequence of at least MAX_MSG_SIZE length without
		   finding
		   the message terminators \r\n. A lame client is messing around with the server. Reset the message
		      buffer and log
		   this malicious behavior.
		 */
		fprintf(stderr,
			"Parse error: message exceeds maximum allowed length. Received by %s\n",
			client->nick == NULL ? "<unregistered>" : client->nick);
		initialize_irc_message(client_msg);
	}
	client_msg->index += read_from_noerr(client,
					     client_msg->msg + client_msg->index,
					     sizeof(client_msg->msg) - client_msg->index);
}

/** Analyzes the incoming messages buffer and the information read from the socket to determine if there's any IRC
   message that can be retrieved from the buffer at the moment.
   @param client_msg The structure representing state information for the sockets reading performed earlier.
   @param msg If a new message is available, `msg` will point to the beginning of a characters sequence that holds an
      IRC message terminated with \\r\\n or \\n. The RFC mandates that IRC messages terminate with \\r\\n, but we found
      clients that do not follow this rule and only terminate messages with \\n. Both approaches are supported: when \\n
      is read, it is assumed that this is the end of the message.
   @return This function returns `MSG_CONTINUE`, which is a negative constant, if no new message is available.
   This means that, at the moment, it is not possible to retrieve a complete IRC message, and that the caller shall wait
      until there is more incoming data in the socket.
   In case of success, the length of a new IRC message is returned (excluding the count for the terminating newline
      character), and `msg` will point to the beginning of the message.
   The message's characters are in `(msg)[0..length-1]`. On success, `length` is guaranteed to be greater than or equal
      to 1, since at least a newline character must have been read.
   Thus, `(msg)[length]` is a valid access: this position always holds a newline character. Consequently, it is safe to
      null terminate an IRC message by issuing `(msg)[length] = $lsquo;\\0&rsquo;`.
   Note, however, that the message may be terminated by \\r\\n (depends on the client); in this case, the caller might
      want to null terminate the message in position `length-1` instead.
   @warning No space allocation takes place, only pointer manipulation.
   @warning Always check for the return codes for this function before using `msg`. Its contents are undefined when the
      return value is not a positive integer.
   @warning It is assumed that an IRC message can terminate with either \\n or \\r\\n. The RFC mandates that every
      message shall be terminated with \\r\\n, but many clients do not follow this and only use \\n. Thus, it is not
      guaranteed that `length-1 >= 0`; the only safe assumption is that `length >= 0` and `(msg)[length] ==
      &lsquo;\\n&rsquo;`.
 */
int next_msg(struct irc_message *client_msg, char **msg)
{
	int i;
	int len;
	char *buf = client_msg->msg;
	for (i = client_msg->last_stop; i < client_msg->index && buf[i] != '\n'; i++)
		;  /* Intentionally left blank */

	/* assert: i == client_msg->index || buf[i] == '\n' */
	if (i == client_msg->index) {
		bring_to_top(client_msg->msg,
			     client_msg->msg + client_msg->msg_begin,
			     client_msg->index -= client_msg->msg_begin);
		client_msg->last_stop = client_msg->index;
		client_msg->msg_begin = 0;
		return MSG_CONTINUE;
	}else {
		/* Wooho, a new message! */
		len = i - client_msg->msg_begin;
		*msg = client_msg->msg + client_msg->msg_begin;
		if ((client_msg->last_stop = client_msg->msg_begin = i + 1) == sizeof(client_msg->msg)) {
			/* Wrap around */
			initialize_irc_message(client_msg);
		}
		return len;
	}
}
