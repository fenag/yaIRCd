#ifndef __INTERPRET_MSG_GUARD__
#define __INTERPRET_MSG_GUARD__
#include "client.h"
/** @file
	@brief Functions responsible for interpreting an IRC message.

	This file describes the functions that are responsible for interpreting an IRC message.
	
	It provides an abstraction layer that deals with messages interpretation arriving from clients.
	
	@author Filipe Goncalves
	@date November 2013
	@see interpretmsg.c
*/

/** A wrapper structure to hold arguments passed to the function called by `client_list_find_and_execute()` */
struct cmd_parse {
	struct irc_client *from; /**<which client issued this command */
	char *prefix; /**<IRC message prefix, as returned by `parse_msg()` */
	char *cmd; /**<IRC message command, as returned by `parse_msg()` */
	char **params; /**<IRC message parameters, as returned by `parse_msg()` */
	int params_size; /**<How many parameters are stored in `params`, as returned by `parse_msg()` */
};

/* Documented in source file */
int cmds_init(void);
void interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
#endif /* __INTERPRET_MSG_GUARD__ */
