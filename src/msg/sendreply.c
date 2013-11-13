#include <unistd.h>
#include <string.h>
#include "protocol.h"
#include "sendreply.h"
#include "wrappers.h"

/** @file
	@brief Implementation for send_* functions.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Sends ERR_NOTREGISTERED to a client who tried to use any command other than NICK, PASS or USER before registering.
	@param client The erratic client to notify
*/
void send_err_notregistered(struct irc_client *client) {
	char desc[] = " :You have not registered";
	send_to(client->socket_fd, ERR_NOTREGISTERED, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

/** Sends ERR_UNKNOWNCOMMAND to a client who seems to be messing around with commands.
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the attempted command.
			   If the IRC message that generated this error had a syntax error, this function shall be called with an empty command, since an ill-formed IRC message may not have a command.
			   The IRC RFC seems to force us to send back a `&lt;command&gt; :Unknown command` reply; if `cmd` is an empty string, `&lt;command&gt;` is replaced by the string `NULL_CMD`.
			   Note that `cmd` must not be a `NULL` pointer. An empty command is a characters sequence such that `*cmd == &lsquo;\0$rsquo;`.
*/
void send_err_unknowncommand(struct irc_client *client, char *cmd) {
	char desc[] = " :Unknown command";
	char emptycmd[] = " NULL_CMD";
	send_to(client->socket_fd, ERR_UNKNOWNCOMMAND, NUMREPLY_WIDTH, 0);
	if (*cmd != '\0') {
		send_to(client->socket_fd, " ", (size_t) 1, 0);
		send_to(client->socket_fd, cmd, strlen(cmd), 0);
	}
	else {
		send_to(client->socket_fd, emptycmd, sizeof(emptycmd)-1, 0);
	}
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

/** Sends ERR_NONICKNAMEGIVEN to a client who issued a NICK command but didn't provide a nick
	@param client The erratic client to notify
*/
void send_err_nonicknamegiven(struct irc_client *client) {
	char desc[] = " :No nickname given";
	send_to(client->socket_fd, ERR_NONICKNAMEGIVEN, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

/** Sends ERR_NEEDMOREPARAMS to a client who issued a command but didn't provide enough parameters for his request to be fulfilled
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence describing the command that is missing parameters.
	@warning `cmd` cannot be the empty string. This should never be a problem, since the message is not syntactically incorrect and there is a command defined.
*/
void send_err_needmoreparams(struct irc_client *client, char *cmd) {
	char desc[] = " :Not enough parameters";
	send_to(client->socket_fd, ERR_NEEDMOREPARAMS, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, " ", 1, 0);
	send_to(client->socket_fd, cmd, strlen(cmd), 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

/** Sends ERR_ERRONEUSNICKNAME to a client who issued a NICK command and chose a nickname that contains invalid characters or exceeds `MAX_NICK_LENGTH`
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_erroneusnickname(struct irc_client *client, char *nick) {
	char desc[] = " :Erroneus nickname";
	send_to(client->socket_fd, ERR_ERRONEUSNICKNAME, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, " ", 1, 0);
	send_to(client->socket_fd, nick, strlen(nick), 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

/** Sends ERR_NICKNAMEINUSE to a client who issued a NICK command and chose a nickname that is already in use.
	@param client The erratic client to notify
	@param cmd A pointer to a null terminated characters sequence with the invalid nickname.
*/
void send_err_nicknameinuse(struct irc_client *client, char *nick) {
	char desc[] = " :Nickname is already in use";
	send_to(client->socket_fd, ERR_NICKNAMEINUSE, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, " ", 1, 0);
	send_to(client->socket_fd, nick, strlen(nick), 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

void send_err_alreadyregistred(struct irc_client *client) {
	char desc[] = " :You may not reregister.";
	send_to(client->socket_fd, ERR_ALREADYREGISTRED, NUMREPLY_WIDTH, 0);
	send_to(client->socket_fd, desc, sizeof(desc)-1, 0);
}

