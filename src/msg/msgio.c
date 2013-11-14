#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "protocol.h"
#include "msgio.h"
#include "wrappers.h"

/** @file
	@brief Implementation for send_* functions.
	
	@author Filipe Goncalves
	@date November 2013
*/

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
		/* Write to SSL socket... */
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
		/* Read from SSL socket... */
		return (ssize_t) 0;
	}
}

/** Sends ERR_NOTREGISTERED to a client who tried to use any command other than NICK, PASS or USER before registering.
	@param client The erratic client to notify
*/
void send_err_notregistered(struct irc_client *client) {
	char desc[] = " :You have not registered\r\n";
	write_to(client, ERR_NOTREGISTERED, NUMREPLY_WIDTH);
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_UNKNOWNCOMMAND to a client who seems to be messing around with commands.
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the attempted command.
			   If the IRC message that generated this error had a syntax error, this function shall be called with an empty command, since an ill-formed IRC message may not have a command.
			   The IRC RFC seems to force us to send back a `&lt;command&gt; :Unknown command` reply; if `cmd` is an empty string, `&lt;command&gt;` is replaced by the string `NULL_CMD`.
			   Note that `cmd` must not be a `NULL` pointer. An empty command is a characters sequence such that `*cmd == &lsquo;\0$rsquo;`.
*/
void send_err_unknowncommand(struct irc_client *client, char *cmd) {
	char desc[] = " :Unknown command\r\n";
	char emptycmd[] = " NULL_CMD";
	write_to(client, ERR_UNKNOWNCOMMAND, NUMREPLY_WIDTH);
	if (*cmd != '\0') {
		write_to(client, " ", (size_t) 1);
		write_to(client, cmd, strlen(cmd));
	}
	else {
		write_to(client, emptycmd, sizeof(emptycmd)-1);
	}
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_NONICKNAMEGIVEN to a client who issued a NICK command but didn't provide a nick
	@param client The erratic client to notify
*/
void send_err_nonicknamegiven(struct irc_client *client) {
	char desc[] = " :No nickname given\r\n";
	write_to(client, ERR_NONICKNAMEGIVEN, NUMREPLY_WIDTH);
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_NEEDMOREPARAMS to a client who issued a command but didn't provide enough parameters for his request to be fulfilled
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the command that is missing parameters.
	@warning `cmd` cannot be the empty string. This should never be a problem, since the message is not syntactically incorrect and there is a command defined.
*/
void send_err_needmoreparams(struct irc_client *client, char *cmd) {
	char desc[] = " :Not enough parameters\r\n";
	write_to(client, ERR_NEEDMOREPARAMS, NUMREPLY_WIDTH);
	write_to(client, " ", 1);
	write_to(client, cmd, strlen(cmd));
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_ERRONEUSNICKNAME to a client who issued a NICK command and chose a nickname that contains invalid characters or exceeds `MAX_NICK_LENGTH`
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_erroneusnickname(struct irc_client *client, char *nick) {
	char desc[] = " :Erroneus nickname\r\n";
	write_to(client, ERR_ERRONEUSNICKNAME, NUMREPLY_WIDTH);
	write_to(client, " ", 1);
	write_to(client, nick, strlen(nick));
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_NICKNAMEINUSE to a client who issued a NICK command and chose a nickname that is already in use.
	@param client The erratic client to notify
	@param nick A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_nicknameinuse(struct irc_client *client, char *nick) {
	char desc[] = " :Nickname is already in use\r\n";
	write_to(client, ERR_NICKNAMEINUSE, NUMREPLY_WIDTH);
	write_to(client, " ", 1);
	write_to(client, nick, strlen(nick));
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends ERR_ALREADYREGISTRED to a client who issued a USER command even though he was already registred.
	@param client The erratic client to notify
*/
void send_err_alreadyregistred(struct irc_client *client) {
	char desc[] = " :You may not reregister.\r\n";
	write_to(client, ERR_ALREADYREGISTRED, NUMREPLY_WIDTH);
	write_to(client, desc, sizeof(desc)-1);
}

/** Sends MOTD to a client.
	@param client The client to send MOTD to.
	@todo Make this configurable, including the server name.
*/
void send_motd(struct irc_client *client) {
	char begin[] = " :- development.yaircd.org Message of the day - \r\n";
	char during[] = " :- ";
	char end[] = " :End of /MOTD command\r\n";
	/* TEMP - make this configurable */
	int i;
	char *motd[] = {
		"Hello, welcome to this IRC server.",
		"This is an experimental server with very few features implemented.",
		"Only PRIVMSG is allowed at the moment, sorry!",
		"A team of highly trained monkeys has been dispatched to deal with this unpleasant situation.",
		"For now, there's really nothing you can do besides guessing who's online and PRIVMSG'ing them",
		"Good luck! :P"
	};
	
	write_to(client, RPL_MOTDSTART, NUMREPLY_WIDTH);
	write_to(client, begin, sizeof(begin)-1);
	
	for (i = 0; i < sizeof(motd)/sizeof(motd[0]); i++) {
		write_to(client, RPL_MOTD, NUMREPLY_WIDTH);
		write_to(client, during, sizeof(during)-1);
		write_to(client, motd[i], strlen(motd[i]));
		write_to(client, "\r\n", 2);
	}
	
	write_to(client, RPL_ENDOFMOTD, NUMREPLY_WIDTH);
	write_to(client, end, sizeof(end)-1);
}

