#ifndef __IRC_CLOAK_GUARD__
#define __IRC_CLOAK_GUARD__
#include "client.h"

/** @file
	@brief Hosts cloaking library
	
	Users' IP addresses in IRC are typically exposed to other IRC users, which can be both inconvenient and dangerous: an attacker can use IRC just to track someone's IP address, so that
	he can nuke their connection with DDoS or something similar.
	
	This module provides host cloaking primitives. Host cloaking consists of encoding a user's host into a hashed value such that channel bans with (or without) wildcards still work, but it is impossible for 
	regular users to know other people's IP address. Thus, a hash that still allows wildcard matching is necessary.
	
	The algorithm used was picked from UnrealIRCd's cloak module (see src/modules/cloak.c in Unreal's source).
	
	Interested readers can learn about the algorithm in the documentation for cloak.c. For other, non-interested readers, it suffices to know that `hide_userhost()` returns a unique cloaked host for a given user
	without leaking information about his IP.
	
	@author Filipe Goncalves
	@date November 2013
	@see cloak.c
*/

/* Documented in C source file */
char *hide_ipv4(char *host);
char *hide_host(char *host);

#endif /* __IRC_CLOAK_GUARD__ */
