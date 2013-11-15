#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include "protocol.h"
#include "msgio.h"
#include "wrappers.h"

/** @file
	@brief Implementation for send_* functions.
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/

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
static void yaircd_send(struct irc_client *client, const char *fmt, ...) {
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
				write_to(client, buffer, bufp);
				bufp = 0;
			}
			buffer[bufp++] = *ptr++;
		}
		else {
			ptr += 2;
			str = va_arg(args, char *);
			if (bufp == sizeof(buffer)-1) {
				write_to(client, buffer, sizeof(buffer)-1);
				bufp = 0;
			}
			while ((len = snprintf(buffer+bufp, sizeof(buffer)-bufp, "%s", str)) >= sizeof(buffer)-bufp) {
				/* assert: buffer[sizeof(buffer)-1] == '\0' */
				str += sizeof(buffer)-bufp-1;
				write_to(client, buffer, sizeof(buffer)-1);
				bufp = 0;
			}
			bufp += len;
		}
	}
	if (bufp > 0) {
		write_to(client, buffer, bufp);
	}
}

/** This function knows how to write to a client. It is an abstraction used by every function that wants to write to a client socket.
	It knows how to deal with plaintext sockets and SSL sockets. No other function in the whole ircd should worry about this.
	@param client The client to notify.
	@param buf A characters sequence, possibly not null-terminated, that shall be written to this client's socket.
	@param len How many characters from `buf` are to be written into this client's socket.
	@note On success, exactly `len` characters will be written to `client`'s socket. On failure, `pthread_exit()` is called, since we apparently lost connection to this client.
*/
inline void write_to(struct irc_client *client, char *buf, size_t len) {
	if (!client->uses_ssl) {
		if (send(client->socket_fd, buf, len, 0) == -1) {
			/* Humm... something went wrong with this socket.
			   The client's process most likely terminated abruptly
			 */
			pthread_exit(NULL);
		}
	}
	else {
		/*poor man in the middle*/
		if((SSL_write(client->ssl, buf, len))==-1) { 
			/*
			  Someone got frustated and possibly cut the physical link.
			 */
			pthread_exit(NULL);
		}		
	}
}

/** This function knows how to read from a client socket. It is an abstraction used by every function that wants to read from a client.
	It knows how to deal with plaintext sockets and SSL sockets. No other function in the whole ircd should worry about this.
	@param client The client to read from.
	@param buf Buffer to store the message read.
	@param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer overflow.
	@return A positive (`> 0`) integer denoting the number of characters read and stored in `buf`.
	@warning `buf` is not null terminated. The caller must use the return value to know where `buf`'s contents end. Typically, the caller will pass a buffer
			  with enough space for `j` characters, but will pass a value for `len` equal to `j-1`. This will bound the number of written characters, `i`, to `j-1`, so that
			  the string can then be null terminated with `buf[i] = &lsquo;\\0&rsquo;`.
	@note If there is a fatal error with this socket, or if the connection from the client side was shut down, this function will call `pthread_exit()`. As a consequence, the returned value is always
		  a positive integer.
*/
inline ssize_t read_from(struct irc_client *client, char *buf, size_t len) {
	ssize_t msg_size;
	if (!client->uses_ssl) {
		msg_size = recv(client->socket_fd, buf, len, 0);
		if (msg_size == 0) {
			/* 0 indicates an orderly shutdown from the client side (typically TCP RST) */
			pthread_exit(NULL);
		}
		if (msg_size == -1) {
			/* Something went wrong with this socket, drop this client */
			pthread_exit(NULL);
		}
		/* assert: msg_size > 0 && msg_size <= len */
		return msg_size;
	}
	else {
		/*Read through SSL*/
		msg_size = SSL_read(client->ssl, buf, len); 
		if(msg_size == -1 || msg_size == 0) { 
			/*
			  Read operation wasn't successful. SSL_get_error() to find. 
			 */
			pthread_exit(NULL);
		}
		return msg_size;
	}
}



/** Sends ERR_NOTREGISTERED to a client who tried to use any command other than NICK, PASS or USER before registering.
	@param client The erratic client to notify
*/
void send_err_notregistered(struct irc_client *client) {
	char *format =
		":%s " ERR_NOTREGISTERED " * :You have not registered\r\n";
	yaircd_send(client, format,
		"development.yaircd.org");
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
		"development.yaircd.org", client->is_registered ? client->nick : "*", *cmd != '\0' ? cmd : "NULL_CMD");
}

/** Sends ERR_NONICKNAMEGIVEN to a client who issued a NICK command but didn't provide a nick
	@param client The erratic client to notify
*/
void send_err_nonicknamegiven(struct irc_client *client) {
	char *format =
		":%s" ERR_NONICKNAMEGIVEN " %s :No nickname given\r\n";
	yaircd_send(client, format,
		"development.yaircd.org", client->is_registered ? client->nick : "*");
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
		"development.yaircd.org", client->is_registered ? client->nick : "*", cmd);
}

/** Sends ERR_ERRONEUSNICKNAME to a client who issued a NICK command and chose a nickname that contains invalid characters or exceeds `MAX_NICK_LENGTH`
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_erroneusnickname(struct irc_client *client, char *nick) {
	char *format =
		":%s " ERR_ERRONEUSNICKNAME " %s %s :Erroneous nickname\r\n";
	yaircd_send(client, format,
		"development.yaircd.org", client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_NICKNAMEINUSE to a client who issued a NICK command and chose a nickname that is already in use.
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_nicknameinuse(struct irc_client *client, char *nick) {
	char *format =
		":%s " ERR_NICKNAMEINUSE " %s %s :Nickname is already in use\r\n";
	yaircd_send(client, format,
		"development.yaircd.org", client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_ALREADYREGISTRED to a client who issued a USER command even though he was already registred.
	@param client The erratic client to notify
*/
void send_err_alreadyregistred(struct irc_client *client) {
	char *format =
		":%s " ERR_ALREADYREGISTRED " %s :You may not reregister.\r\n";
	
	yaircd_send(client, format,
		"development.yaircd.org", client->nick);
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
		"development.yaircd.org", client->nick, "development.yaircd.org",
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick,
		"development.yaircd.org", client->nick);
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
		"development.yaircd.org", client->nick, client->nick, client->username, client->hostname,
		"development.yaircd.org", client->nick, "development.yaircd.org", "v1.0",
		"development.yaircd.org", client->nick, "15/11/2013",
		"development.yaircd.org", client->nick, "development.yaircd.org", "v1.0", "UMODES=xTR", "CHANMODES=mvil");
}

