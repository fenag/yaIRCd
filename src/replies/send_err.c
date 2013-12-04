#include "send_err.h"
#include "msgio.h"
#include "serverinfo.h"

/** @file
	@brief send_err* functions
	
	This file defines the set of functions that send error replies to a client.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Sends ERR_NOTREGISTERED to a client who tried to use any command other than NICK, PASS or USER before registering.
   @param client The erratic client to notify
 */
void send_err_notregistered(struct irc_client *client)
{
	char *format =
		":%s " ERR_NOTREGISTERED " * :You have not registered\r\n";
	yaircd_send(client, format,
		    get_server_name());
}

/** Sends ERR_UNKNOWNCOMMAND to a client who seems to be messing around with commands.
   @param client The erratic client to notify
   @param cmd A pointer to a null terminated characters sequence describing the attempted command.
   If the IRC message that generated this error had a syntax error, this function shall be called with an empty command,
      since an ill-formed IRC message may not have a command.
   The IRC RFC seems to force us to send back a `&lt;command&gt; :Unknown command` reply; if `cmd` is an empty string,
      `&lt;command&gt;` is replaced by the string `NULL_CMD`.
   Note that `cmd` must not be a `NULL` pointer. An empty command is a characters sequence such that `cmd ==
      &lsquo;\0$rsquo;`.
 */
void send_err_unknowncommand(struct irc_client *client, char *cmd)
{
	char *format =
		":%s " ERR_UNKNOWNCOMMAND " %s %s :Unknown command\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->is_registered ? client->nick : "*", *cmd != '\0' ? cmd : "NULL_CMD");
}

/** Sends ERR_NONICKNAMEGIVEN to a client who issued a NICK command but didn't provide a nick
   @param client The erratic client to notify
 */
void send_err_nonicknamegiven(struct irc_client *client)
{
	char *format =
		":%s " ERR_NONICKNAMEGIVEN " %s :No nickname given\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->is_registered ? client->nick : "*");
}

/** Sends ERR_NEEDMOREPARAMS to a client who issued a command but didn't provide enough parameters for his request to be
   fulfilled
   @param client The erratic client to notify
   @param cmd A pointer to a null terminated characters sequence describing the command that is missing parameters.
   @warning `cmd` cannot be the empty string. This should never be a problem, since the message is not syntactically
      incorrect and there is a command defined.
 */
void send_err_needmoreparams(struct irc_client *client, char *cmd)
{
	char *format =
		":%s " ERR_NEEDMOREPARAMS " %s %s :Not enough parameters\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->is_registered ? client->nick : "*", cmd);
}

/** Sends ERR_ERRONEUSNICKNAME to a client who issued a NICK command and chose a nickname that contains invalid
   characters or exceeds `MAX_NICK_LENGTH`
   @param client The erratic client to notify
   @param nick A pointer to a null terminated characters sequence with the invalid nickname.
 */
void send_err_erroneusnickname(struct irc_client *client, char *nick)
{
	char *format =
		":%s " ERR_ERRONEUSNICKNAME " %s %s :Erroneous nickname\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_NICKNAMEINUSE to a client who issued a NICK command and chose a nickname that is already in use.
   @param client The erratic client to notify
   @param nick A pointer to a null terminated characters sequence with the invalid nickname.
 */
void send_err_nicknameinuse(struct irc_client *client, char *nick)
{
	char *format =
		":%s " ERR_NICKNAMEINUSE " %s %s :Nickname is already in use\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->is_registered ? client->nick : "*", nick);
}

/** Sends ERR_ALREADYREGISTRED to a client who issued a USER command even though he was already registred.
   @param client The erratic client to notify
 */
void send_err_alreadyregistred(struct irc_client *client)
{
	char *format =
		":%s " ERR_ALREADYREGISTRED " %s :You may not reregister.\r\n";

	yaircd_send(client, format,
		    get_server_name(), client->nick);
}

/** Sends ERR_NORECIPIENT to a client trying to send a message without a recipient.
   @param client The erratic client to notify
   @param cmd The command that originated the error
 */
void send_err_norecipient(struct irc_client *client, char *cmd)
{
	const char *format =
		":%s " ERR_NORECIPIENT " %s :No recipient given (%s)\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->nick, cmd);
}

/** Sends ERR_NOTEXTTOSEND to a client trying to send an empty message to another client.
   @param client The erratic client to notify
 */
void send_err_notexttosend(struct irc_client *client)
{
	const char *format =
		":%s " ERR_NOTEXTTOSEND " %s :No text to send\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->nick);
}

/** Sends ERR_NOSUCHNICK to a client who supplied a nonexisting target.
   @param client The erratic client to notify
   @param nick The nonexisting target
 */
void send_err_nosuchnick(struct irc_client *client, char *nick)
{
	const char *format =
		":%s " ERR_NOSUCHNICK " %s %s :No such nick/channel\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->nick, nick);
}

/** Sends ERR_NOSUCHCHANNEL to a client who supplied an invalid channel name.
   @param client The erratic client to notify
   @param chan The invalid channel name
 */
void send_err_nosuchchannel(struct irc_client *client, char *chan)
{
	const char *format =
		":%s " ERR_NOSUCHCHANNEL " %s %s :No such channel\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->nick, chan);
}

/** Sends ERR_NOTONCHANNEL to a client who tried to part a channel he's not in.
   @param client The erratic client to notify
   @param chan The channel name
 */
void send_err_notonchannel(struct irc_client *client, char *chan)
{
	const char *format =
		":%s " ERR_NOTONCHANNEL " %s %s :You're not on that channel\r\n";
	yaircd_send(client, format,
		    get_server_name(), client->nick, chan);
}

/** Sends ERR_TOOMANYCHANNELS to a client who tried to join a channel, but already hit the max channels limit, as configured in yaircd.conf.
   @param client The erratic client to notify
   @param chan The channel name
 */
void send_err_toomanychannels(struct irc_client *client, char *chan) {
	const char *format =
		":%s " ERR_TOOMANYCHANNELS " %s %s :You have joined too many channels\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick, chan);
}

/** Sends ERR_NOORIGIN to a PONG reply from a client who didn't indicate the PING origin.
   @param client The erratic client to notify
 */
void send_err_noorigin(struct irc_client *client) {
	const char *format =
		":%s " ERR_NOORIGIN " %s :No origin specified\r\n";
	yaircd_send(client, format,
		get_server_name(), client->nick);
}
