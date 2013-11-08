#ifndef __P_LIMITS_GUARD__
#define __P_LIMITS_GUARD__

/** @file
	@brief IRC Protocol defined limits
	
	Limits that arise from the IRC protocol specification are defined in this header file.
	See RFC 1459 for further information.
	
	@author Filipe Goncalves
	@date November 2013
	@todo Change this file name to avoid confusion with the standard file <limits.h>
*/

/** Max. message size, including terminating \\r\\n, as specified in Section 2.3 */
#define MAX_MSG_SIZE 512

/** Max. number of parameters in an IRC message, as specified in Section 2.3 */
#define MAX_IRC_PARAMS 15

#endif /* __P_LIMITS_GUARD__ */
