#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include <ev.h>
#include "protocol.h"
#include "msgio.h"
#include "wrappers.h"
#include "serverinfo.h"

/** @file
	@brief Implementation for send_* functions.
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/

/** Similar to `write_to()`, but in case of socket error, `pthread_exit()` is called.
	@param client The client to read from.
	@param buf Buffer to store the message read.
	@param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer overflow.
*/
inline void write_to_noerr(struct irc_client *client, char *buf, size_t len) {
	if (write_to(client, buf, len) == -1) {
		pthread_exit(NULL);
	}
}

/** Similar to `read_from()`, but in case of socket error, `pthread_exit()` is called.
	@param client The client to read from.
	@param buf Buffer to store the message read.
	@param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer overflow.
	@return A positive integer denoting the number of characters read.
*/
inline ssize_t read_from_noerr(struct irc_client *client, char *buf, size_t len) {
	ssize_t msg_size;
	if ((msg_size = read_from(client, buf, len)) <= 0) {
		pthread_exit(NULL);
	}
	return msg_size;
}

/** A printf equivalent version for yaIRCd that sends a set of arbitrarily long IRC messages into a client's socket.
	This function shall be called everytime a new message is to be written to a client. No other functions in the entire IRCd can write to a client's socket.
	It is an abstraction to the sockets interface. The internal implementation uses a buffer capable of storing a message with at most `MAX_MSG_SIZE` characters. If data to be delivered is bigger
	than the buffer size, this function will break it up in blocks of `MAX_MSG_SIZE` characters and write them one by one into the client's socket.
	Thus, this abstraction allows anyone to call this function with a format string arbitrarily long. The format string can hold multiple IRC messages.
	The primary role for this function is to minimize the number of `write()` syscalls. It is imperative that the code using this function tries to pass as many data as it can at the moment, so that big
	chunks of information are sent.
	@param client The client to send information to
	@param fmt The format string, pretty much like printf. However, only the `%s` formatter is supported.
	@param ... Optional parameters to match `%s` formatters.
	@warning Always check to make sure that the number of formatters match the number of optional parameters passed. GCC will not issue a warning as it would for printf, because this is not part of the standard library.
	@warning It is assumed that the code using this function is well behaved; in particular, it is assumed that `fmt` is well formed, and that a `%` is always followed by an `s`, because only `%s` is supported.
*/
void yaircd_send(struct irc_client *client, const char *fmt, ...) {
	const char *ptr;
	char *str;
	char buffer[MAX_MSG_SIZE+1];
	size_t bufp;
	int len;
	va_list args;
	va_start(args, fmt);
	
	ptr = fmt;
	bufp = 0;
	while (*ptr != '\0') {
		if (*ptr != '%') {
			if (bufp == sizeof(buffer)-1) {
				write_to_noerr(client, buffer, bufp);
				bufp = 0;
			}
			buffer[bufp++] = *ptr++;
		}
		else {
			ptr += 2;
			str = va_arg(args, char *);
			if (bufp == sizeof(buffer)-1) {
				write_to_noerr(client, buffer, sizeof(buffer)-1);
				bufp = 0;
			}
			while ((len = snprintf(buffer+bufp, sizeof(buffer)-bufp, "%s", str)) >= sizeof(buffer)-bufp) {
				/* assert: buffer[sizeof(buffer)-1] == '\0' */
				str += sizeof(buffer)-bufp-1;
				write_to_noerr(client, buffer, sizeof(buffer)-1);
				bufp = 0;
			}
			bufp += len;
		}
	}
	if (bufp > 0) {
		write_to_noerr(client, buffer, bufp);
	}
}

/** Works pretty much the same way as `sprintf()`, but it never returns a number greater than or equal to `size`. As a consequence,
	when the output is truncated, the buffer is terminated with CR LF, and the rest of the output is discarded.
	Typically, `size` will be `MAX_MSG_SIZE+1`, to allow for a null terminator.
	Since this function uses `vsnprintf()` internally, the buffer is always null terminated.
	@param buf Output buffer
	@param size How many characters, at most, can be written in `buf`
	@param msg Format string with the same syntax as every `printf()` family function
	@param ... Optional arguments matching the format string
	@return A number less than `size` indicating how many characters, excluding the null terminator, were written.
*/
int cmd_print_reply(char *buf, size_t size, const char *msg, ...) {
	int ret;
	va_list args;
	va_start(args, msg);
	ret = vsnprintf(buf, size, msg, args);
	va_end(args);
	if (ret >= size) {
		buf[size-2] = '\n';
		buf[size-3] = '\r';
		ret = size-1;
	}
	return ret;
}

/** Sends ERR_NOTREGISTERED to a client who tried to use any command other than NICK, PASS or USER before registering.
	@param client The erratic client to notify
*/
void send_err_notregistered(struct irc_client *client) {
	char *format =
		":%s " ERR_NOTREGISTERED " * :You have not registered\r\n";
	yaircd_send(client, format,
		get_server_name());
}

/** Sends ERR_UNKNOWNCOMMAND to a client who seems to be messing around with commands.
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the attempted command.
			   If the IRC message that generated this error had a syntax error, this function shall be called with an empty command, since an ill-formed IRC message may not have a command.
			   The IRC RFC seems to force us to send back a `&lt;command&gt; :Unknown command` reply; if `cmd` is an empty string, `&lt;command&gt;` is replaced by the string `NULL_CMD`.
			   Note that `cmd` must not be a `NULL` pointer. An empty command is a characters sequence such that `*cmd == &lsquo;\0$rsquo;`.
*/
void send_err_unknowncommand(struct irc_client *client, char *cmd) {
	char *format =
		":%s " ERR_UNKNOWNCOMMAND " %s %s :Unknown command\r\n";
	yaircd_send(client, format,
		get_server_name(), client->is_registered ? client->nick : "*", *cmd != '\0' ? cmd : "NULL_CMD");
}

/** Sends ERR_NONICKNAMEGIVEN to a client who issued a NICK command but didn't provide a nick
	@param client The erratic client to notify
*/
void send_err_nonicknamegiven(struct irc_client *client) {
	char *format =
		":%s " ERR_NONICKNAMEGIVEN " %s :No nickname given\r\n";
	yaircd_send(client, format,
		get_server_name(), client->is_registered ? client->nick : "*");
}

/** Sends ERR_NEEDMOREPARAMS to a client who issued a command but didn't provide enough parameters for his request to be fulfilled
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the command that is missing parameters.
	@warning `cmd` cannot be the empty string. This should never be a problem, since the message is not syntactically incorrect and there is a command defined.
*/
void send_err_needmoreparams(struct irc_client *client, char *cmd) {
	char *format =
		":%s " ERR_NEEDMOREPARAMS " %s %s :Not enough parameters\r\n";
	yaircd_send(client, format,
		get_server_name(), client->is_registered ? client->nick : "*", cmd);
}

/** Sends ERR_ERRONEUSNICKNAME to a client who issued a NICK command and chose a nickname that contains invalid characters or exceeds `MAX_NICK_LENGTH`
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_erroneusnickname(struct irc_client *client, char *nick) {
	char *format =
		":%s " ERR_ERRONEUSNICKNAME " %s %s :Erroneous nickname\r\n";
	yaircd_send(client, format,
		get_server_name(), client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_NICKNAMEINUSE to a client who issued a NICK command and chose a nickname that is already in use.
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_nicknameinuse(struct irc_client *client, char *nick) {
	char *format =
		":%s " ERR_NICKNAMEINUSE " %s %s :Nickname is already in use\r\n";
	yaircd_send(client, format,
		get_server_name(), client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_ALREADYREGISTRED to a client who issued a USER command even though he was already registred.
	@param client The erratic client to notify
*/
void send_err_alreadyregistred(struct irc_client *client) {
	char *format =
		":%s " ERR_ALREADYREGISTRED " %s :You may not reregister.\r\n";
	
	yaircd_send(client, format,
		get_server_name(), client->nick);
}

/** Sends MOTD to a client.
	@param client The client to send MOTD to.
	@todo Make this configurable, including the server name.
*/
void send_motd(struct irc_client *client) {
	const char *format =
		":%s " RPL_MOTDSTART " %s :- %s Message of the day - \r\n"
		":%s " RPL_MOTD " %s :- Hello, welcome to this IRC server.\r\n"
		":%s " RPL_MOTD " %s :- This is an experimental server with very few features implemented.\r\n"
		":%s " RPL_MOTD " %s :- Only PRIVMSG is allowed at the moment, sorry!\r\n"
		":%s " RPL_MOTD " %s :- A team of highly trained monkeys has been dispatched to deal with this unpleasant situation.\r\n"
		":%s " RPL_MOTD " %s :- For now, there's really nothing you can do besides guessing who's online and PRIVMSG'ing them.\r\n"
		":%s " RPL_MOTD " %s :- Good luck! :P\r\n"
		":%s " RPL_ENDOFMOTD " %s :End of /MOTD command\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, get_server_name(),
		get_server_name(), client->nick,
		get_server_name(), client->nick,
		get_server_name(), client->nick,
		get_server_name(), client->nick,
		get_server_name(), client->nick,
		get_server_name(), client->nick,
		get_server_name(), client->nick);
}

/** Sends the welcome message to a newly registred user
	@param client The client to greet.
	@todo Make this correct to present settings properly
*/
void send_welcome(struct irc_client *client) {
	const char *format = 
		":%s " RPL_WELCOME " %s :Welcome to the Internet Relay Network %s!%s@%s\r\n"
		":%s " RPL_YOURHOST " %s :Your host is %s, running version %s\r\n"
		":%s " RPL_CREATED " %s :This server was created %s\r\n"
		":%s " RPL_MYINFO " %s :%s %s %s %s\r\n";

	yaircd_send(client, format,
		get_server_name(), client->nick, client->nick, client->username, client->hostname,
		get_server_name(), client->nick, get_server_name(), "v1.0",
		get_server_name(), client->nick, "15/11/2013",
		get_server_name(), client->nick, get_server_name(), "v1.0", "UMODES=xTR", "CHANMODES=mvil");
}

/** Sends ERR_NORECIPIENT to a client trying to send a message without a recipient.
	@param client The erratic client to notify
	@param cmd The command that originated the error
*/
void send_err_norecipient(struct irc_client *client, char *cmd) {
	const char *format =
		":%s " ERR_NORECIPIENT " %s :No recipient given (%s)\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, cmd);
}

/** Sends ERR_NOTEXTTOSEND to a client trying to send an empty message to another client.
	@param client The erratic client to notify
*/
void send_err_notexttosend(struct irc_client *client) {
	const char *format =
		":%s " ERR_NOTEXTTOSEND " %s :No text to send\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick);
}

/** Sends ERR_NOSUCHNICK to a client who supplied a nonexisting target.
	@param client The erratic client to notify
	@param nick The nonexisting target
*/
void send_err_nosuchnick(struct irc_client *client, char *nick) {
	const char *format =
		":%s " ERR_NOSUCHNICK " %s %s :No such nick/channel\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, nick);
}

/** Sends ERR_NOSUCHCHANNEL to a client who supplied an invalid channel name.
	@param client The erratic client to notify
	@param chan The invalid channel name
*/
void send_err_nosuchchannel(struct irc_client *client, char *chan) {
	const char *format =
		":%s " ERR_NOSUCHCHANNEL " %s %s :No such channel\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, chan);
}

/** Sends ERR_NOTONCHANNEL to a client who tried to part a channel he's not in.
	@param client The erratic client to notify
	@param chan The channel name
*/
void send_err_notonchannel(struct irc_client *client, char *chan) {
	const char *format = 
		":%s " ERR_NOTONCHANNEL " %s %s :You're not on that channel\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, chan);
}

/** Sends a generic PRIVMSG command notification to a given target. The destination can either be a channel or a user.
	@param from The message's author
	@param to Message's recipient. This can be the other end of a private conversation, or it can be a regular channel user receiving a message on a channel.
	@param dest The destination of the message. If it is a private conversation, it will just be `to`'s nickname; otherwise, it is the channel name.
	@param msg The message to deliver.
*/
void notify_privmsg(struct irc_client *from, struct irc_client *to, char *dest, char *msg) {
	char message[MAX_MSG_SIZE+1];
	const char *format =
		":%s!%s@%s PRIVMSG %s :%s\r\n";
	cmd_print_reply(message, sizeof(message), format, from->nick, from->username, from->realname, dest, msg);
	client_enqueue(&to->write_queue, message);
	ev_async_send(to->ev_loop, &to->async_watcher);
}

/** Function used when a client wants to flush his messages write queue.
	This is indirectly called by the queue library in `client_queue_foreach()` (see `queue_async_cb()`).
	@param message The message dequeued in the current iteration.
	@param arg A generic argument passed by the original function that called `client_queue_foreach()`.
	@warning It is very important to remember that this function will be executed in an atomic block, holding a lock to a client's queue.
			 Thus, it is imperative that this function does not call `pthread_exit()`.
*/
inline void msg_flush(char *message, void *arg) {
	/* We ignore possible errors that may arise during a write. We use write_to(), not write_to_noerr(),
	   because write_to_noerr() calls pthread_exit() if things go wrong, and we can't call pthread_exit() here,
	   since we're holding a lock to a queue. Things would fall apart if we did so.
	   The thread will eventually be notified that the connection was broken. In the meantime, we just keep writing as if
	   nothing happened.
	 */
	(void) write_to((struct irc_client *) arg, message, strlen(message));
	free(message);
}
