#ifndef __PARSEMSG_GUARD__
#define __PARSEMSG_GUARD__
#include "protocol.h"

/** @file
	@brief IRC Messages parser
	
	Functions that are used to parse an IRC message.
	See RFC section 2.3 to learn about IRC messages syntax.
	
	@author Filipe Goncalves
	@date November 2013
*/

/* Documented in parsemsg.c */
int parse_msg(char *buf, char **prefix, char **cmd, char *params[MAX_IRC_PARAMS], int *params_filled);

#endif /* __PARSEMSG_GUARD__ */
