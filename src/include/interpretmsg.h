/** @file
	@brief Functions responsible for interpreting an IRC message.

	This file describes the functions that are responsible for interpreting an IRC message.
	
	It provides an abstraction layer that deals with messages interpretation arriving from clients.
	
	@author Filipe Goncalves
	@date November 2013
	@see interpretmsg.c
*/

/* Documented in source file */
int interpret_msg(struct irc_client *client, char *prefix, char *cmd, char *params[], int params_size);
