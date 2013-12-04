#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include <ev.h>
#include "protocol.h"
#include "msgio.h"

/** @file
   @brief Implementation for send_ functions.
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
 */

/** Similar to `write_to()`, but in case of socket error, `terminate_session()` is called using `BAD_WRITE_QUIT_MSG` as a quit message.
   @param client The client to read from.
   @param buf Buffer to store the message read.
   @param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer
      overflow.
 */
inline void write_to_noerr(struct irc_client *client, char *buf, size_t len)
{
	if (write_to(client, buf, len) == -1) {
		terminate_session(client, BAD_WRITE_QUIT_MSG);
	}
}

/** Similar to `read_from()`, but in case of socket error, `terminate_session()` is called using `BAD_READ_QUIT_MSG` as a quit message.
   @param client The client to read from.
   @param buf Buffer to store the message read.
   @param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer
      overflow.
   @return A positive integer denoting the number of characters read.
 */
inline ssize_t read_from_noerr(struct irc_client *client, char *buf, size_t len)
{
	ssize_t msg_size;
	if ((msg_size = read_from(client, buf, len)) <= 0) {
		terminate_session(client, BAD_READ_QUIT_MSG);
	}
	return msg_size;
}

/** A printf equivalent version for yaIRCd that sends a set of arbitrarily long IRC messages into a client's socket.
	Note that when expanding the format string, which is done inside `snprintf()`, each IRC message can expand to something bigger than `MAX_MSG_SIZE`.
	It is the upper code's responsability to ensure this doesn't happen. Not that that's a problem, it's just that it's a little bit annoying for the client
	to receive messages that exceed the limits defined in the protocol.
	This function never overflows, since it uses two levels of buffers: first, a buffer of size `MAX_MSG_SIZE` is attempted; if that isn't enough, a buffer of the exact needed
	size is dynamically allocated, the resulting string is generated and printed to this buffer, sent to the client, and freed.
   @param client The client to send information to
   @param fmt The format string, pretty much like printf.
   @param ... Optional parameters to match formatters.
   @warning Always check to make sure that the number of formatters match the number of optional parameters passed. GCC
      will not issue a warning as it would for printf, because this is not part of the standard library.
   @warning It is assumed that the code using this function is well behaved; in particular, it is assumed that `fmt` is well formed.
 */
void yaircd_send(struct irc_client *client, const char *fmt, ...)
{
	va_list args;
	char buf[MAX_MSG_SIZE+1];
	char *large_buf;
	int size;
	
	va_start(args, fmt);
	size = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (size >= sizeof(buf)) {
		if ((large_buf = malloc((size_t) size+1)) == NULL) {
			fprintf(stderr, "::msgio.c:yaircd_send(): Needed to allocate %d long buffer, but no memory is availabe.\nOriginal message: %s\n", size+1, fmt);
			return;
		}
		va_start(args, fmt);
		vsnprintf(large_buf, size+1, fmt, args);
		va_end(args);
		write_to_noerr(client, large_buf, (size_t) size);
		free(large_buf);
		return;
	}
	write_to_noerr(client, buf, size);
}

/** Works pretty much the same way as `sprintf()`, but it never returns a number greater than or equal to `size`. As a
   consequence,when the output is truncated, the buffer is terminated with CR LF, and the rest of the output is
   discarded.
   Typically, `size` will be `MAX_MSG_SIZE+1`, to allow for a null terminator.
   Since this function uses `vsnprintf()` internally, the buffer is always null terminated.
   @param buf Output buffer
   @param size How many characters, at most, can be written in `buf`. Since `buf` is null terminated and IRC messages are terminated with \\r\\n, `size` must be greater than or equal to 3.
   @param msg Format string with the same syntax as every `printf()` family function
   @param ... Optional arguments matching the format string
   @return A number less than `size` indicating how many characters, excluding the null terminator, were written.
 */
int cmd_print_reply(char *buf, size_t size, const char *msg, ...)
{
	int ret;
	va_list args;
	va_start(args, msg);
	ret = vsnprintf(buf, size, msg, args);
	va_end(args);
	if (ret >= size) {
		buf[size - 2] = '\n';
		buf[size - 3] = '\r';
		ret = size - 1;
	}
	return ret;
}
