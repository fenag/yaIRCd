#include "client.h"

/** @file
	@brief Functions that send a reply to a command issued by an IRC user
	
	This file provides a set of functions to send various replies to an IRC user in the sequence of a command sent to the server.
	
	@author Filipe Goncalves
	@date November 2013
	@see msgio.c
*/

/** A macro that knows how to write to a client. It is an abstraction used by every function that wants to write to a client socket.
	It knows how to deal with plaintext sockets and SSL sockets. No other function in the whole ircd should worry about this.
	@param client The client to notify.
	@param buf A characters sequence, possibly not null-terminated, that shall be written to this client's socket.
	@param len How many characters from `buf` are to be written into this client's socket.
	@note On success, exactly `len` characters will be written to `client`'s socket, and the macro evaluates to `0`. On failure, the macro evaluates to `-1`.
*/
#define write_to(client,buf,len) ((client)->uses_ssl ? SSL_write((client)->ssl, (buf), (len)) : send((client)->socket_fd, (buf), (len), 0))

/** A macro that knows how to read from a client socket. It is an abstraction used by every function that wants to read from a client.
	It knows how to deal with plaintext sockets and SSL sockets. No other function in the whole ircd should worry about this.
	On success, the macro evaluates to a positive integer of type `ssize_t` denoting the number of characters read on success. If the other end closed the connection, the macro evaluates to `0`.
	In case of failure, the macro evaluates to `-1`.
	@param client The client to read from.
	@param buf Buffer to store the message read.
	@param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer overflow.
	@warning `buf` is not null terminated. The caller must use the evaluated value to know where `buf`'s contents end. Typically, the caller will pass a buffer
			  with enough space for `j` characters, but will pass a value for `len` equal to `j-1`. This will bound the number of written characters, `i`, to `j-1`, so that
			  the string can then be null terminated with `buf[i] = &lsquo;\\0&rsquo;`.
*/
#define read_from(client,buf,len) ((client)->uses_ssl ? SSL_read((client)->ssl, (buf), (len)) : recv((client)->socket_fd, (buf), (len), 0))

/* Functions documented in the source file */
void yaircd_send(struct irc_client *client, const char *fmt, ...);
int cmd_print_reply(char *buf, size_t size, char *msg, ...);
void send_err_notregistered(struct irc_client *client);
void send_err_unknowncommand(struct irc_client *client, char *cmd);
void send_err_nonicknamegiven(struct irc_client *client);
void send_err_needmoreparams(struct irc_client *client, char *cmd);
void send_err_erroneusnickname(struct irc_client *client, char *nick);
void send_err_nicknameinuse(struct irc_client *client, char *nick);
void send_err_alreadyregistred(struct irc_client *client);
void send_motd(struct irc_client *client);
void send_welcome(struct irc_client *client);
void send_err_norecipient(struct irc_client *client, char *cmd);
void send_err_notexttosend(struct irc_client *client);
void send_err_nosuchnick(struct irc_client *client, char *nick);
void send_err_nosuchchannel(struct irc_client *client, char *chan);
inline void msg_flush(char *message, void *arg);
inline void write_to_noerr(struct irc_client *client, char *buf, size_t len);
inline ssize_t read_from_noerr(struct irc_client *client, char *buf, size_t len);

