#ifndef __YAIRCD_SEND_ERR_GUARD__
#define __YAIRCD_SEND_ERR_GUARD__
#include "client.h"

/** @file
	@brief send_err* functions
	
	This file defines the set of functions that send error replies to a client.
	
	@author Filipe Goncalves
	@date November 2013
*/

/* Documented in send_err.c */
void send_err_notregistered(struct irc_client *client);
void send_err_unknowncommand(struct irc_client *client, char *cmd);
void send_err_nonicknamegiven(struct irc_client *client);
void send_err_needmoreparams(struct irc_client *client, char *cmd);
void send_err_erroneusnickname(struct irc_client *client, char *nick);
void send_err_nicknameinuse(struct irc_client *client, char *nick);
void send_err_alreadyregistred(struct irc_client *client);
void send_err_norecipient(struct irc_client *client, char *cmd);
void send_err_notexttosend(struct irc_client *client);
void send_err_nosuchnick(struct irc_client *client, char *nick);
void send_err_nosuchchannel(struct irc_client *client, char *chan);
void send_err_notonchannel(struct irc_client *client, char *chan);
void send_err_toomanychannels(struct irc_client *client, char *chan);
void send_err_noorigin(struct irc_client *client);
void send_err_nomotd(struct irc_client *client);
#endif /* __YAIRCD_SEND_ERR_GUARD__ */
