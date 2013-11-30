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

/** Similar to `write_to()`, but in case of socket error, `pthread_exit()` is called.
   @param client The client to read from.
   @param buf Buffer to store the message read.
   @param len Maximum length of the message. This is usually bounded by the size of `buf`. This parameter avoids buffer
      overflow.
 */
inline void write_to_noerr(struct irc_client *client, char *buf, size_t len)
{
	if (write_to(client, buf, len) == -1) {
		pthread_exit(NULL);
	}
}

/** Similar to `read_from()`, but in case of socket error, `pthread_exit()` is called.
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
		pthread_exit(NULL);
	}
	return msg_size;
}

/** A printf equivalent version for yaIRCd that sends a set of arbitrarily long IRC messages into a client's socket.
   This function shall be called everytime a new message is to be written to a client. No other functions in the entire
      IRCd can write to a client's socket.
   It is an abstraction to the sockets interface. The internal implementation uses a buffer capable of storing a message
      with at most `MAX_MSG_SIZE` characters. If data to be delivered is biggerthan the buffer size, this function will
      break it up in blocks of `MAX_MSG_SIZE` characters and write them one by one into the client's socket.
   Thus, this abstraction allows anyone to call this function with a format string arbitrarily long. The format string
      can hold multiple IRC messages.
   The primary role for this function is to minimize the number of `write()` syscalls. It is imperative that the code
      using this function tries to pass as many data as it can at the moment, so that bigchunks of information are sent.
   @param client The client to send information to
   @param fmt The format string, pretty much like printf. However, only the `%s` formatter is supported.
   @param ... Optional parameters to match `%s` formatters.
   @warning Always check to make sure that the number of formatters match the number of optional parameters passed. GCC
      will not issue a warning as it would for printf, because this is not part of the standard library.
   @warning It is assumed that the code using this function is well behaved; in particular, it is assumed that `fmt` is
      well formed, and that a `%` is always followed by an `s`, because only `%s` is supported.
 */
void yaircd_send(struct irc_client *client, const char *fmt, ...)
{
	const char *ptr;
	char *str;
	char buffer[MAX_MSG_SIZE + 1];
	size_t bufp;
	int len;
	va_list args;
	va_start(args, fmt);

	ptr = fmt;
	bufp = 0;
	while (*ptr != '\0') {
		if (*ptr != '%') {
			if (bufp == sizeof(buffer) - 1) {
				write_to_noerr(client, buffer, bufp);
				bufp = 0;
			}
			buffer[bufp++] = *ptr++;
		}else {
			ptr += 2;
			str = va_arg(args, char *);
			if (bufp == sizeof(buffer) - 1) {
				write_to_noerr(client, buffer, sizeof(buffer) - 1);
				bufp = 0;
			}
			while ((len =
					snprintf(buffer + bufp, sizeof(buffer) - bufp, "%s",
						 str)) >= sizeof(buffer) - bufp) {
				/* assert: buffer[sizeof(buffer)-1] == '\0' */
				str += sizeof(buffer) - bufp - 1;
				write_to_noerr(client, buffer, sizeof(buffer) - 1);
				bufp = 0;
			}
			bufp += len;
		}
	}
	if (bufp > 0) {
		write_to_noerr(client, buffer, bufp);
	}
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
