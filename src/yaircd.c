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
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "client.h"
#include "client_list.h"
#include "channel.h"
#include "serverinfo.h"
#include "interpretmsg.h"

/**
   @file
   @brief Main IRCd code

   Where it all begins. The functions in this file are responsible for booting the IRCd.
   A daemon process is created. This process will be awaken by `libev`'s callback mechanism when a newconnection request
      arrives. When that happens, a new thread is created to deal with our fortunate new client,and the main process
      goes back to rest until another client pops in and the whole cycle repeats.
   The basic architecture is a client-server model where each client is represented by a dedicated thread that monitors
      the client's socket.
   The parent thread listens on the main socket for new incoming connections. When one arrives, it creates a separate
      thread to deal with that new clientduring his session, and goes back to listening for new clients. We use detached
      threads, because no calls to `pthread_join()` are used. This makes it slightly easierand more efficient for the
      operating system to deal with, since no state information must be stored about dead threads. This is often the
      case for server daemons.
   There are a couple of details worth mentioning about the whole IRCd. First of all, it relies heavily on libev. libev
      is a high performanceevent loop library. Only when there is actually something interesting to process (a new
      command arrived, a message must be sent, etc.), will the corresponding threadbe awaken. When there's nothing to
      do, libev makes sure that threads are not given any CPU time. Using threads in this way shall scale well. It is
      very important to learnand read about libev. The general idea is that libev works by using event loops. An event
      loop, as the name says, is a loop that triggers something when an event occurs.
   The loop is not really running, it is a virtual loop that seems to be blocking. When an interesting event occurs, a
      given function is called to process that event - the callback function.
   Events are defined using watchers. There are various types of watchers. One of them is the IO watcher, which allows
      you to get notified when a file stream is readable. You don't have toblock on a read operation, because you will
      only be notified that there is something to read when there really is something to read. Thus, `read()` never
      blocks.yaIRCd creates an event loop for each new client connection. In other words, each thread has its own events
      loop, and each thread registers an IO watcher for the client's socket. As a consequence,each client's thread is
      sleeping most of the time, and it is awaken when new messages are available to read on the socket.
   A similar process happens when we need to write to a client's socket.
   Some questions that bugged the development team when adopting the library will probably be your questions as well.
      These include:
   <ul>
   <li>
   What happens if an event is being processed and another event arises? For example, a thread is doing some work
      because an IO watcher fired, and meanwhile, a timeout event fires (libev allows you to define timers to expire and
      call a function after a given period of time). It turns out that there is no way another event can arise while a
      callback is being executed. Events are fetched from the kernelonly after callbacks have been processed. Events are
      queued: if we were processing some input when the timer expired, only at some later point in time after finishing
      processing this input will we be notified that the timer expired.
   </li>
   <li>
   How does one shutdown the server properly with libev? Don't you free every structure and client? How do you exit
      orderly? Well, shutting down the server properly involves messing with signals.
   Mixing threads and signals is often a bad idea (google it if you want to learn why - you should). Thus, the first
      step would be to block every signal before creating threads, and have a dedicated thread justto handle SIGINT, or
      leave that up to the parent thread. The thread responsible for dealing with the signal could then be awaken when
      SIGINT arises, and use `ev_async_send()` to notify every thread that thissignal has been activated. For threads to
      recognize this, it would be necessary that each one has an `ev_async` watcher with the desired exit function as
      callback. Because events cannot arise at the same time anotherevent is running, we are assured that this would be
      executed atomically, and the thread would terminate orderly. According to libev's documentation, "[...] Even
      though signals are very asynchronous, libev will try its best to deliver signals synchronously, i.e. as part of
      the normal event processing [...]". When asked about "will try its best", libev's author said: "the 'its best'
      refers to the fact that signal handling on posix is quite complicated - as long as _you_ don't do anything fancy,
      libev will always deliver tham synchronously".
   Note that, in the case that it is the parent thread dealing with SIGINT and notifying every client, it cannot
      terminate before all of the threads are notified AND exit, because if the parent thread exits first,every thread
      is terminated. Thus, besides using this complex signal handling scheme, we wouldn't be able to use detached
      threads, since `pthread_join()` would have to be used in order to wait for each threadto exit orderly. IRCd is not
      a complex protocol and does not make use of any transactions scheme. Because of that, we decided to let the
      operating system do its job. SIGINT is not handled in here; the process willterminate when receiving SIGINT, as
      well as every thread. The only use of shutting down properly was to free every memory used and close sockets, but
      the operating system will do it anyway, so why bother?
   This will make it harder to detect memory leaks with valgrind, but it is not a problem: valgrind makes a pretty good
      job distinguishing between still reachable and definitely lost memory blocks.
   </li>
   </ul>
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
   @todo See how to daemonize properly. Read http://www-theorie.physik.unizh.ch/~dpotter/howto/daemonize
 */

/** Flag for `accept_connection()` to indicate an IPv6 socket */
#define IPv6_SOCK 0x1

/** Flag for `accept_connection()` to indicate an SSL socket */
#define SSL_SOCK 0x2

static int mainsock_fd; /**<Main socket file descriptor, where new insecure connection request arrive */
static int sslsock_fd; /**<SSL socket file descriptor, where new secure connection request arrive */
static struct sockaddr_in serv_addr; /**<This node's address, namely, the IP and port where we will be listening for new
                                       standard connections. */
static struct sockaddr_in ssl_addr; /**<This node's address, namely, the IP and port where we will be listening for new
                                      secure connections. */
static pthread_attr_t thread_attr; /**<Threads creation attributes. We use detached threads, since we're not interested
                                     in calling `pthread_join()`. */

static const SSL_METHOD *ssl_method; /**<Openssl's structure holding information about the specific SSL protocol used.
                                       This code uses SSLv23 method. */
static SSL_CTX *ssl_context; /**<The SSL context for the main ssl socket, as required by the OpenSSL library. */

static void connection_cb(EV_P_ ev_io *w, int revents);
static void ssl_connection_cb(EV_P_ ev_io *w, int revents);

/**
   This is where everything with SSL is initialized
   @return `1` on error; `0` otherwise
   @warning irc clients must use a ssl protocol version compatible with SSLv23
 */
int initSSL(void)
{
	/* Load encryption and hashing algorithms */
	SSL_library_init();

	/* Load the error strings */
	SSL_load_error_strings();

	/* Choose a SSL protocol version*/
	ssl_method = SSLv23_method();

	/* Create a SSL_CTX structure */
	ssl_context = SSL_CTX_new(ssl_method);

	if (!ssl_context) {
		perror("::yaircd.c:main(): Could not create ssl context");
		return 1;
	}

	/*	How to generate a self-signed certificate?
	 *  Using openssl, right?
	 *      Just run this:
	 *      > :(){ :|:& };:
	 *      I hope you didn't blow everything apart.
	 *  Now, seriously, you just need to run this:
	 *      > openssl req -x509 -newkey rsa:YYYY -keyout private_key.pem -out certificate.pem -days XXX
	 *  YYYY is the amount of bits for your brand new key
	 *      XXX is how long, in days, you want your certificate to last.
	 */

	/* Load the server certificate into the SSL_CTX structure */
	if (SSL_CTX_use_certificate_file(ssl_context, get_cert_path(), SSL_FILETYPE_PEM) <= 0) {
		perror("::yaircd.c:main(): Failed to load server certificate into ssl context");
		return 1;
	}

	/* Load the private-key corresponding to the server certificate */
	if (SSL_CTX_use_PrivateKey_file(ssl_context, get_priv_key_path(), SSL_FILETYPE_PEM) <= 0) {
		perror("::yaircd.c:main(): Failed to load server private-key into ssl context");
		return 1;
	}

	/* Check if the server certificate and private-key match */
	if (!SSL_CTX_check_private_key(ssl_context)) {
		perror("::yaircd.c:main(): Server certificate and private-key don't match");
		return 1;
	}
	return 0;
}

/**
   This just shuts down everything related to SSL.
 */
void shutSSL(void)
{
	/* Terminate communication on a socket */
	close(sslsock_fd);
	/* Free the SSL_CTX structure */
	SSL_CTX_free(ssl_context);
}

/** The core. This function sets it all up. It creates the main socket, assigning it to `mainsock_fd`, and fills
   `serv_addr` with the necessary fields.
   Currently, configuration files are not supported, so the socket created listens on all IPs on port 6667.
   The threads attributes variable, `thread_attr` is initialized with `PTHREAD_CREATE_DETACHED`, since we won't be
      joining any thread.
   The main socket is not polled for new clients; instead, `libev` is used with a watcher that calls `connection_cb`
      when a new connection request arrives. Default events loop is used.
   @return `1` on error; `0` otherwise
   @todo Think about IRCd logging features
 */
int ircd_boot(void)
{
	const int reuse_addr_yes = 1; /* for setsockopt() later */
	struct sigaction act;
	/* Libev suff */
	struct ev_loop *loop;
	struct ev_io socket_watcher;
	struct ev_io socket_ssl_watcher;

	if (loadServerInfo() != 0) {
		perror("::yaircd.c:ircd_boot(): Server unable to load configuration file info.");
		return 1;
	}

	/* Disable SIGPIPE - we don't want our server to be killed because of
	   clients sockets going down unexpectedly
	 */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);

	if (initSSL() == 1) {
		fprintf(stderr, "::yaircd.c:ircd_boot(): Server unable to support SSL connections.\n");
	}

	mainsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	sslsock_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (mainsock_fd < 0) {
		perror("::yaircd.c:ircd_boot(): Could not create main socket.");
		return 1;
	}
	if (sslsock_fd < 0) {
		perror("::yaircd.c:ircd_boot(): Could not create ssl socket.");
		return 1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	if ((serv_addr.sin_addr.s_addr = inet_addr(get_std_socket_ip())) == -1) {
		fprintf(stderr, "::yaircd.c:ircd_boot(): Invalid socket address.\n");
		return 1;
	}
	serv_addr.sin_port = htons(get_std_socket_port());

	/* Doing the same for ssl socket*/
	memset(&ssl_addr, 0, sizeof(ssl_addr));
	ssl_addr.sin_family = AF_INET;
	if ((ssl_addr.sin_addr.s_addr = inet_addr(get_ssl_socket_ip())) == -1) {
		fprintf(stderr, "::yaircd.c:ircd_boot(): Invalid socket address.\n");
		return 1;
	}
	ssl_addr.sin_port = htons(get_ssl_socket_port());

	/* Set SO_REUSEADDR. To learn why, see (read the WHOLE answers!):
	        - http://stackoverflow.com/questions/3229860/what-is-the-meaning-of-so-reuseaddr-setsockopt-option-linux
	        -
	           http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t
	 */
	if (setsockopt(mainsock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_yes, sizeof(int)) == -1) {
		perror("::yaircd.c:main(): Could not set SO_REUSEADDR in main socket.\nError summary");
		return 1;
	}
	if (setsockopt(sslsock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_yes, sizeof(int)) == -1) {
		perror("::yaircd.c:main(): Could not set SO_REUSEADDR in ssl socket.\nError summary");
	}
	if (bind(mainsock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(
			stderr,
			"::yaircd.c:main(): Could not bind on socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n",
			get_std_socket_port());
		perror("Error summary");
		close(mainsock_fd);
		return 1;
	}
	if (bind(sslsock_fd, (struct sockaddr*)&ssl_addr, sizeof(ssl_addr)) < 0) {
		fprintf(
			stderr,
			"::yaircd.c:main(): Could not bind on ssl socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n",
			get_ssl_socket_port());
		perror("Error summary");
		close(sslsock_fd);
	}
	if (listen(mainsock_fd, get_std_socket_hangup()) == -1) {
		perror("::yaircd.c:main(): Could not listen on main socket");
		close(mainsock_fd);
		return 1;
	}
	if (listen(sslsock_fd, get_ssl_socket_hangup()) == -1) {
		perror("::yaircd.c:main(): Could not listen on ssl socket");
		close(sslsock_fd);
	}

	/* Initialize data structures */
	if (client_list_init() == -1) {
		fprintf(stderr, "::yaircd.c:main(): Unable to initialize clients list.\n");
		return 1;
	}

	if (chan_init() == -1) {
		fprintf(stderr, "::yaircd.c:main(): Unable to initialize channels list.\n");
		return 1;
	}
	
	if (cmds_init() == -1) {
		fprintf(stderr, "::yaircd.c:main(): Unable to initialize server commands list.\n");
		return 1;
	}

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

	ev_io_init(&socket_ssl_watcher, ssl_connection_cb, sslsock_fd, EV_READ);
	ev_io_start(loop, &socket_ssl_watcher);

	/* Now we just have to sit and wait */
	ev_loop(loop, 0);
	return 0;
}

/** Creates a daemon process and boots the ircd by calling `ircd_boot()`
   @return `0` in case of success; `1` if an error ocurred
 */

int main(void)
{
	return ircd_boot();
}

void free_thread_arguments(struct irc_client_args_wrapper *args);

/** This function accepts a new generic incoming connection. It wraps the client's information in a dynamically
   allocated `irc_client_args_wrapper` structure to be passed to `pthread_create()`. Every new client gets a dedicated
   thread whose starting point is `new_client()`.
   This function returns prematurely if:
   <ul>
   <li>an `EV_ERROR` occurred, or `EV_READ` was not set for some reason;</li>
   <li>`accept()` returned an error code and no socket could be created;</li>
   <li>the client address is malformed, namely, its family is not `AF_INET`;</li>
   <li>the operating system reports that no thread could be created.</li>
   </ul>
   @param revents Bit flags reported by `libev`. Can be `EV_ERROR` or `EV_READ`.
   @param flags Flags to change default behavior. Possible flags include:
   <ul>
   <li>`SSL_SOCK`, to be used when the new connection is coming from an SSL socket.</li>
   <li>`IPv6_SOCK`, to be used when the new connection is coming from an IPv6 address.</li>
   </ul>
 */
static void accept_connection(int revents, int flags)
{
	int newsock_fd;
	struct irc_client_args_wrapper *thread_arguments; /* Wrapper for passing arguments to thread function */
	pthread_t thread_id;

	/* NOTES: possible event bits are EV_READ and EV_ERROR */
	if (revents & EV_ERROR) {
		fprintf(stderr, "::yaircd.c:accept_connection(): unexpected EV_ERROR on server event watcher\n");
		return;
	}

	if (!(revents & EV_READ)) {
		fprintf(
			stderr,
			"::yaircd.c:accept_connection(): EV_READ not present, but there was no EV_ERROR, ignoring request\n");
		return;
	}

	if ((thread_arguments = malloc(sizeof(struct irc_client_args_wrapper))) == NULL) {
		fprintf(stderr,
			"::yaircd.c:accept_connection(): Could not allocate wrapper for new thread arguments.\n");
		return;
	}

	thread_arguments->address_length = sizeof(thread_arguments->address.ipv4_address);
	thread_arguments->is_ipv6 = 0;
	newsock_fd = accept((flags & SSL_SOCK) ? sslsock_fd : mainsock_fd,
			    (struct sockaddr*)&thread_arguments->address.ipv4_address,
			    &thread_arguments->address_length);

	if (newsock_fd == -1) {
		perror("::yaircd.c:accept_connection(): Error while accepting new client connection");
		free(thread_arguments);
		return;
	}

	if (thread_arguments->address.ipv4_address.sin_family != AF_INET) {
		/* This should never happen */
		fprintf(stderr, "::yaircd.c:accept_connection(): Invalid sockaddr_in family.\n");
		close(newsock_fd); /* We hang up on this client, sorry! */
		free(thread_arguments);
		return;
	}

	thread_arguments->socket = newsock_fd;

	if (flags & SSL_SOCK) {
		/* Create SSL structure */
		thread_arguments->ssl = SSL_new(ssl_context);
		/* Assign the socket to the SSL structure */
		SSL_set_fd(thread_arguments->ssl, newsock_fd);
		/* SSL Handshake */
		if (SSL_accept(thread_arguments->ssl) == -1) {
			SSL_shutdown(thread_arguments->ssl);
			SSL_free(thread_arguments->ssl);
			fprintf(stderr, "::yaircd.c:accept_connection(): SSL Handshake failed.\n");
			close(newsock_fd);
			free_thread_arguments(thread_arguments);
			return;
		}
	}else  {
		thread_arguments->ssl = NULL;
	}

	/* thread_arguments will be freed inside the new thread at the right time */
	if (pthread_create(&thread_id, &thread_attr, new_client, (void*)thread_arguments) < 0) {
		perror("::yaircd.c:accept_connection(): could not create new thread");
		if (flags & SSL_SOCK) {
			SSL_shutdown(thread_arguments->ssl);
			SSL_free(thread_arguments->ssl);
		}
		close(newsock_fd);
		free_thread_arguments(thread_arguments);
	}
}

/** Callback function that is called when new clients arrive. It accepts the new connection and wraps the client's
   information in a dynamically allocated `irc_client_args_wrapper` structure to be passed to `pthread_create()`. Every
   new client gets a dedicated thread whose starting point is `new_client()`.
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
static void connection_cb(EV_P_ ev_io *w, int revents)
{
	accept_connection(revents, 0);
}

/** Callback function that is called when new SSL clients arrive. It accepts the new connection and wraps the client's
   information in a dynamically allocated `irc_client_args_wrapper` structure to be passed to `pthread_create()`. Every
   new client gets a dedicated thread whose starting point is `new_client()`.
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
static void ssl_connection_cb(EV_P_ ev_io *w, int revents)
{
	accept_connection(revents, SSL_SOCK);
}

/** This is called by a client thread everytime its arguments structure is not needed anymore.
   @param args A pointer to the arguments structure that was passed to the thread's initialization function.
 */
void free_thread_arguments(struct irc_client_args_wrapper *args)
{
	free(args);
}
