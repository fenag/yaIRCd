#ifndef __YAIRCD_F_WRAPPERS__
#define __YAIRCD_F_WRAPPERS__
/** @file
	@brief yaIRCd function wrappers
	
	This file defines a set of common wrappers, or useful macros, to widely used and known functions.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Make strcmp intuitive to read. This macro makes it possible to have code like this:
	@code
		...
		if (strcmp(a, ==, b)) { ... }
		...
	@endcode
	Which is more intuitive than the traditional equality test involving `== 0` when the strings are equal.
	This idea is taken from Expert C Programming - Deep C Secrets; it is presented as a useful software heuristic
 */
#define strcmp(a,R,b) (strcmp(a,b) R 0)

/** As described for `strcmp` macro, make `strcasecmp` equally intuitive */
#define strcasecmp(a,R,b) (strcasecmp(a,b) R 0)

#endif /* __YAIRCD_F_WRAPPERS__ */
