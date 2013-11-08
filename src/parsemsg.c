#include "protocol/limits.h"
#include "include/parsemsg.h"
#include <ctype.h>

/** @file
	@brief IRC Messages parser implementation
	
	This file implements functions that are used to parse an IRC message.
	See RFC section 2.3 to learn about IRC messages syntax.
	
	@author Filipe Goncalves
	@date November 2013
*/

/** Searches for the next position in `str` that is not a white space. Tabs are not considered white space.
	It is assumed that the target string is null terminated. If there's no next position that is not a white space,
	a pointer to the end of the string is returned.
	@param str The target string
	@return Pointer to the first position in `str` that is not a white space.
*/
static char *skipspaces(char *str) {
	for (; *str != '\0' && *str == ' '; str++)
		; /* Intentionally left blank */
	return str;
}

/** Searches for the next position in `str` that is a white space. Tabs are not considered white space.
	It is assumed that the target string is null terminated. If there's no next position that is a white space,
	a pointer to the end of the string is returned.
	@param str The target string
	@return Pointer to the first position in `str` that is a white space.
*/
static char *skipnonspaces(char *str) {
	for (; *str != '\0' && *str != ' '; str++)
		; /* Intentionally left blank */
	return str;
}

/** Reads the parameters in an IRC message.
	@param buf Pointer to the beginning of a characters sequence with the parameters.
	@param params Array of pointers where each element will point to the beginning of a parameter. Note that the last parameter may be a sequence of characters with spaces if ':' was used.
	@return <ul>
				<li>`-1` if the maximum number of parameters allowed by the protocol (15) was exceeded, in which case the contents of `params[i]` is undefined;</li>
				<li>Otherwise, the number of parameters read and inserted in `params` is returned.</li>
			</ul>
	@warning `buf` is destructively changed. In particular, white space separators will be overwritten with `NUL` characters. `params[i]` points to a position in `buf`, thus, no memory is allocated in this function, 
			  and no copies of `buf` take place. This function only manipulates pointers.
	@warning It is assumed that `buf` is null terminated, and that no \\r\\n trailing characters exist.
	@warning `params` is assumed to contain enough space for the maximum number of parameters allowed for an IRC message (as of this writing, 15). It can be bigger than that, but this function will ignore any additional space.
*/
static int read_params(char *buf, char *params[MAX_IRC_PARAMS]) {
	int pos;
	char *next;
	
	pos = 0;
	for (buf = skipspaces(buf), next = skipnonspaces(buf); *buf != '\0' && *buf != ':'; buf = skipspaces(buf), next = skipnonspaces(buf)) {
		/* assert:
			*buf != ' ' && *buf != '\0' && *buf != ':'
			*next == ' ' || *next == '\0'
			=> assert(buf != next) [assures that this is a non-empty sequence, as required by the RFC]
		*/
		if (pos == MAX_IRC_PARAMS) {
			return -1; /* Sorry buddy, no buffer overflow hacks! */
		}
		params[pos++] = buf;
		if (*next == '\0') {
			buf = next;
		}
		else {
			buf = next+1;
			*next = '\0';
		}
	}
	if (*buf == ':' && (buf = skipspaces(buf+1)) != '\0') {
		/* We allow for spaces after ':' in the parameters list. The RFC does not; but this is harmless :) */
		if (pos < MAX_IRC_PARAMS) {
			params[pos++] = buf;
		}
		else {
			return -1;
		}
	}
	return pos;
}

/** Parses an IRC messsage and splits it up into its different components. The format for an IRC message is thoroughly described in Section 2.3.1 of the IRC specification (see doc/rfc.html).
	This function acts more like a tokenizer - note that no semantic checking takes place. It is a purely syntax based parser.
	The message buffer is manipulated and its contents will be different after the function returns. Namely, space separators can be overwritten with a `NUL` character.
	This function can be safely called by different threads, as long as each thread passes different arguments.
	@param buf Buffer containing the new message. It is assumed that the buffer is null terminated.
	@param size Number of characters in `buf`, not counting with the null terminating character. IRC messages must end with \\r\\n. This parameter is used to quickly check if this message follows the rule.
	@param prefix When a prefix exists, `*prefix` will point to the beginning of the prefix field (first character after `:`) in `buf`. The end of the prefix is determined by a `NUL` character.
	@param cmd `*cmd` will point to the beginning of the command field (first non space character in the message; first non space character after prefix if one exists). The end is determined by a `NUL` character.
	@param params An array of pointers to a character sequence. Each array element points to a position in `buf` denoting a parameter. Parameters are null terminated.
	@param params_filled `*params_filled` will hold the number of parameters parsed. Thus, after returning, it is valid to reference any position `i` in `params` as long as `i >= 0 && i < *params_filled`.
	@return <ul>
				<li>`-1` if an error occurred, meaning there was a syntax error. Syntax errors that can be detected include: 
					 <ol>
						<li>The case that a message only contains a prefix field, in which case the value of `*prefix` is undefined;</li>
						<li>The case that a message does not conform to the RFC specification that it must end with \\r\\n.</li>
						<li>The case that there are more parameters in a command than the maximum allowed by the RFC, in which case the contents of `params[i]` is undefined, but no buffer overflow conditions occur.</li>
					</ol>
				</li>
				<li>`0` if no prefix exists in the parsed message (in which case the value of `*prefix` remains unchanged)</li>
				<li>`1` if there's a prefix in the parsed message</li>
	@warning `buf` is destructively changed. `*prefix`, `*cmd`, and `params[i]` all point to different positions in `buf`. No memory is allocated inside this function, only pointer manipulation takes place.
	@warning When `*params_filled > 0`, note that, according to the RFC, it is possible that `params[*params_filled-1]` points to a parameter with spaces. This is the case everytime a trailing parameter was prefixed with `:`
	@warning `params` is assumed to contain enough space for the maximum number of parameters allowed for an IRC message (as of this writing, 15). It can be bigger than that, but this function will ignore any additional space.
*/
int parse_msg(char *buf, int size, char **prefix, char **cmd, char *params[MAX_IRC_PARAMS], int *params_filled) {
	char *current;
	char *next;
	int ret;
	
	if (size < 2 || buf[size-1] != '\n' || buf[size-2] != '\r') {
		return -1;
	}
	buf[size-2] = '\0';
	current = skipspaces(buf);
	ret = 0;
	if (*current == ':') {
		next = skipnonspaces(current+1);
		if (*next == '\0' || next == current+1) {
			/* Sender said there was a prefix, but there's no prefix */
			return -1;
		}
		*next = '\0';
		*prefix = current+1;
		ret = 1;
		current = next+1;
	}
	current = skipspaces(current);
	if (*current == '\0') {
		return -1;
	}
	/* Parse command */
	if (isdigit((unsigned char) *current)) {
		if (isdigit((unsigned char) *(current+1)) && isdigit((unsigned char) *(current+2)) && (*(current+3) == '\0' || *(current+3) == ' ')) {
			*cmd = current;
			next = current+3;
		}
		else {
			return -1;
		}
	}
	else {
		for (next = current; *next != '\0' && isalpha((unsigned char) *next); next++)
			; /* Intentionally left blank */
		if (*next != '\0' && *next != ' ') {
			/* Invalid command */
			return -1;
		}
	}
	*params_filled = 0;
	/* assert: *next == ' ' || *next == '\0' */
	if (*next != '\0' && (*params_filled = read_params(next+1, params)) == -1) {
		return -1;
	}
	*next = '\0';
	*cmd = current;
	return ret;
}


/* Debug */
/*
#include <stdio.h>
#include <string.h>

void prtbuf(char *b) {
	for (; *b != '\0'; b++) {
		if (*b == '\r') {
			putchar('\\');
			putchar('r');
		}
		else if (*b == '\n') {
			putchar('\\');
			putchar('n');
		}
		else
			putchar(*b);
	}
	putchar('\n');
}


int main(void) {
	char buf[513];
	char *prefix, *cmd, *params[MAX_IRC_PARAMS]; 
	int params_size, ret;
	int i;
	size_t len;
	
	while (1) {
		fgets(buf, 512, stdin);
		len = strlen(buf);
		if (len > 0) {
			buf[len-1] = '\r';
			buf[len] = '\n';
			buf[len+1] = '\0';
		}
		printf("Buffer contents:\n");
		prtbuf(buf);
		ret = parse_msg(buf, len+1, &prefix, &cmd, params, &params_size);
		if (ret == -1) {
			printf("Invalid message!\n");
		}
		else {
			printf("Prefix: %s\n", ret == 1 ? prefix : "<no prefix>");
			printf("Command: %s\n", cmd);
			for (i = 0; i < params_size; i++) {
				printf("Parameter %d: %s\n", i+1, params[i]);
			}
		}
	}
	
	return 0;
}
*/
