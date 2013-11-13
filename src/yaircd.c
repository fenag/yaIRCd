#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>
#include <ev.h>
#include <string.h>
#include "client.h"
#include "client_list.h"

/** @file
	@brief Main IRCd code

	Where it all begins. The functions in this file are responsible for booting the IRCd.
	A daemon process is created. This process will be awaken by `libev`'s callback mechanism when a new
	connection request arrives. When that happens, a new thread is created to deal with our fortunate new client,
	and the main process goes back to rest until another client pops in and the whole cycle repeats.
	
	@author Filipe Goncalves
	@date November 2013
	@todo See how to daemonize properly. Read http://www-theorie.physik.unizh.ch/~dpotter/howto/daemonize
	@todo Think about adding configuration file support (.conf)
	@todo Allow to bind for multiple IPs
*/

/** How many clients are allowed to be waiting while the main process is creating a thread for a freshly arrived user. This can be safely incremented to 5 */
#define SOCK_MAX_HANGUP_CLIENTS 5

static int mainsock_fd; /**<Main socket file descriptor, where new connection request arrive */
static struct sockaddr_in serv_addr; /**<This node's address, namely, the IP and port where we will be listening for new connections. */
static struct sockaddr_in cli_addr; /**<The client address structure that will hold information for new connected users. */
static socklen_t clilen; /**<Length of the client's address. This is needed for `accept()` */
static pthread_attr_t thread_attr; /**<Threads creation attributes. We use detached threads, since we're not interested in calling `pthread_join()`. */

static void connection_cb(EV_P_ ev_io *w, int revents);

/** The core. This function sets it all up. It creates the main socket, assigning it to `mainsock_fd`, and fills `serv_addr` with the necessary fields.
	Currently, configuration files are not supported, so the socket created listens on all IPs on port 6667.
	The threads attributes variable, `thread_attr` is initialized with `PTHREAD_CREATE_DETACHED`, since we won't be joining any thread.
	The main socket is not polled for new clients; instead, `libev` is used with a watcher that calls `connection_cb` when a new connection request arrives. Default events loop is used.
	@return `1` on error; `0` otherwise
	@todo Think about IRCd logging features
*/
int ircd_boot(void) {
	int portno = 6667;
	const int reuse_addr_yes = 1; /* for setsockopt() later */
	struct sigaction act;
	/* Libev suff */
	struct ev_loop *loop;
	struct ev_io socket_watcher;

	/* Disable SIGPIPE - we don't want our server to be killed because of
	   clients sockets going down unexpectedly
	 */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);
	
	mainsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (mainsock_fd < 0) {
		perror("::yaircd.c:main(): Could not create main socket");
		return 1;
	}
	
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	/* Store port number in network byte order */
	serv_addr.sin_port = htons(portno);
	
	/* Set SO_REUSEADDR. To learn why, see (read the WHOLE answers!):
		- http://stackoverflow.com/questions/3229860/what-is-the-meaning-of-so-reuseaddr-setsockopt-option-linux
		- http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t
	 */
	if (setsockopt(mainsock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_yes, sizeof(int)) == -1) {
		perror("::yaircd.c:main(): Could not set SO_REUSEADDR in main socket.\nError summary");
		return 1;
	}
	
	if (bind(mainsock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, "::yaircd.c:main(): Could not bind on socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n", portno);
		perror("Error summary");
		close(mainsock_fd);
		return 1;
	}
	if (listen(mainsock_fd, SOCK_MAX_HANGUP_CLIENTS) == -1) {
		perror("::yaircd.c:main(): Could not listen on main socket");
		close(mainsock_fd);
		return 1;
	}
	
	/* Initialize data structures */
	if (client_list_init() == -1) {
		fprintf(stderr, "::yaircd.c:main(): Unable to initialize clients list.\n");
		return 1;
	}
	clilen = sizeof(cli_addr);
	/* Initialize thread creation attributes */
	if (pthread_attr_init(&thread_attr) != 0) {
		/* On Linux, this will never happen */
		perror("::yaircd.c:main(): Could not initialize thread attributes");
		return 1;
	}
	/* We want detached threads */
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	/* At this point, we're ready to accept new clients. Set the callback function for new connections */
	loop = EV_DEFAULT;
	ev_io_init(&socket_watcher, connection_cb, mainsock_fd, EV_READ);
	ev_io_start(loop, &socket_watcher);	
	/* Now we just have to sit and wait */
	ev_loop(loop, 0);	
	return 0;
}

/** Creates a daemon process and boots the ircd by calling `ircd_boot()`
	@return `0` in case of success; `1` if an error ocurred
*/
int main(void) {
	/*if (daemon(1,0) == -1) {
		perror("::yaircd.c:main(): Could not daemonize");
		return 1;
	}
	else {*/
		return ircd_boot();
	/*}*/
}

void free_thread_arguments(struct irc_client_args_wrapper *args);
/** Callback function that is called when new clients arrive. It accepts the new connection and wraps the client's information in a dynamically allocated `irc_client_args_wrapper` structure to be passed to
	`pthread_create()`. Every new client gets a dedicated thread whose starting point is `new_client()`.
	This function returns prematurely if:
		<ul>
			<li>an `EV_ERROR` occurred, or `EV_READ` was not set for some reason;</li>
			<li>`accept()` returned an error code and no socket could be created;</li>
			<li>the client address is malformed, namely, its family is not `AF_INET`;</li>
			<li>the operating system reports that no thread could be created.</li>
		</ul>
	@param w The watcher that caused this callback to execute. Always comes from the main default loop.
	@param revents Bit flags reported by `libev`. Can be `EV_ERROR` or `EV_READ`.
*/
static void connection_cb(EV_P_ ev_io *w, int revents) {
    int newsock_fd;
	struct irc_client_args_wrapper *thread_arguments; /* Wrapper for passing arguments to thread function */
	pthread_t thread_id;
	
	/* NOTES: possible event bits are EV_READ and EV_ERROR */
    if (revents & EV_ERROR) {
        fprintf(stderr, "::yaircd.c:connection_cb(): unexpected EV_ERROR on server event watcher\n");
        return;
    }
	
	if (!(revents & EV_READ)) {
		fprintf(stderr, "::yaircd.c:connection_cb(): EV_READ not present, but there was no EV_ERROR, ignoring request\n");
		return;
	}
	
	newsock_fd = accept(mainsock_fd, (struct sockaddr *) &cli_addr, &clilen);
	
	if (newsock_fd == -1) {
		perror("::yaircd.c:connection_cb(): Error while accepting new client connection");
		return;
	}
	
	if (cli_addr.sin_family != AF_INET) {
		/* This should never happen */
		fprintf(stderr, "::yaircd.c:connection_cb(): Invalid sockaddr_in family.\n");
		close(newsock_fd); /* We hang up on this client, sorry! */
		return;
	}
	
	if ((thread_arguments = malloc(sizeof(struct irc_client_args_wrapper))) == NULL) {
		fprintf(stderr, "::yaircd.c:connection_cb(): Could not allocate wrapper for new thread arguments.\n");
		close(newsock_fd);
		return;
	}
	if ((thread_arguments->ip_addr = strdup(inet_ntoa(cli_addr.sin_addr))) == NULL) {
		fprintf(stderr, "::yaircd.c:connection_cb(): Could not allocate wrapper for new thread arguments.\n");
		free(thread_arguments);
		close(newsock_fd);
		return;
	}
	
	thread_arguments->socket = newsock_fd;
	
	/* thread_arguments will be freed inside the new thread at the right time */
	if (pthread_create(&thread_id, &thread_attr, new_client, (void *) thread_arguments) < 0) {
		perror("::yaircd.c:connection_cb(): could not create new thread");
		close(newsock_fd);
		free_thread_arguments(thread_arguments);
	}
}

/** This is called by a client thread everytime its arguments structure is not needed anymore.
	@param args A pointer to the arguments structure that was passed to the thread's initialization function.
*/
void free_thread_arguments(struct irc_client_args_wrapper *args) {
	free(args->ip_addr);
	free(args);
}
