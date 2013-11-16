#include <stdio.h>
#include <libconfig.h>
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
#include "yaIRCd.h"

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/** @file
	@brief Main IRCd code

	Where it all begins. The functions in this file are responsible for booting the IRCd.
	A daemon process is created. This process will be awaken by `libev`'s callback mechanism when a new
	connection request arrives. When that happens, a new thread is created to deal with our fortunate new client,
	and the main process goes back to rest until another client pops in and the whole cycle repeats.
	
	The basic architecture is a client-server model where each client is represented by a dedicated thread that monitors the client's socket.
	The parent thread listens on the main socket for new incoming connections. When one arrives, it creates a separate thread to deal with that new client
	during his session, and goes back to listening for new clients. We use detached threads, because no calls to `pthread_join()` are used. This makes it slightly easier
	and more efficient for the operating system to deal with, since no state information must be stored about dead threads. This is often the case for server daemons.
	
	There are a couple of details worth mentioning about the whole IRCd. First of all, it relies heavily on libev. libev is a high performance
	event loop library. Only when there is actually something interesting to process (a new command arrived, a message must be sent, etc.), will the corresponding thread
	be awaken. When there's nothing to do, libev makes sure that threads are not given any CPU time. Using threads in this way shall scale well. It is very important to learn
	and read about libev. The general idea is that libev works by using event loops. An event loop, as the name says, is a loop that triggers something when an event occurs.
	The loop is not really running, it is a virtual loop that seems to be blocking. When an interesting event occurs, a given function is called to process that event - the callback function.
	Events are defined using watchers. There are various types of watchers. One of them is the IO watcher, which allows you to get notified when a file stream is readable. You don't have to
	block on a read operation, because you will only be notified that there is something to read when there really is something to read. Thus, `read()` never blocks.
	yaIRCd creates an event loop for each new client connection. In other words, each thread has its own events loop, and each thread registers an IO watcher for the client's socket. As a consequence,
	each client's thread is sleeping most of the time, and it is awaken when new messages are available to read on the socket.
	A similar process happens when we need to write to a client's socket.
	Some questions that bugged the development team when adopting the library will probably be your questions as well. These include:
	<ul>
		<li>What happens if an event is being processed and another event arises? For example, a thread is doing some work because an IO watcher fired, and meanwhile, a timeout event fires (libev allows you
		    to define timers to expire and call a function after a given period of time). It turns out that there is no way another event can arise while a callback is being executed. Events are fetched from the kernel
			only after callbacks have been processed. Events are queued: if we were processing some input when the timer expired, only at some later point in time after finishing processing this input will we be notified 
			that the timer expired.
		</li>
		<li>How does one shutdown the server properly with libev? Don't you free every structure and client? How do you exit orderly? Well, shutting down the server properly involves messing with signals. 
			Mixing threads and signals is often a bad idea (google it if you want to learn why - you should). Thus, the first step would be to block every signal before creating threads, and have a dedicated thread just
			to handle SIGINT, or leave that up to the parent thread. The thread responsible for dealing with the signal could then be awaken when SIGINT arises, and use `ev_async_send()` to notify every thread that this
			signal has been activated. For threads to recognize this, it would be necessary that each one has an `ev_async` watcher with the desired exit function as callback. Because events cannot arise at the same time another
			event is running, we are assured that this would be executed atomically, and the thread would terminate orderly. According to libev's documentation, "[...] Even though signals are very asynchronous, libev will try its 
			best to deliver signals synchronously, i.e. as part of the normal event processing [...]". When asked about "will try its best", libev's author said: "the 'its best' refers to the fact that signal handling on posix is 
			quite complicated - as long as _you_ don't do anything fancy, libev will always deliver tham synchronously".
			Note that, in the case that it is the parent thread dealing with SIGINT and notifying every client, it cannot terminate before all of the threads are notified AND exit, because if the parent thread exits first,
			every thread is terminated. Thus, besides using this complex signal handling scheme, we wouldn't be able to use detached threads, since `pthread_join()` would have to be used in order to wait for each thread
			to exit orderly. IRCd is not a complex protocol and does not make use of any transactions scheme. Because of that, we decided to let the operating system do its job. SIGINT is not handled in here; the process will
			terminate when receiving SIGINT, as well as every thread. The only use of shutting down properly was to free every memory used and close sockets, but the operating system will do it anyway, so why bother?
			This will make it harder to detect memory leaks with valgrind, but it is not a problem: valgrind makes a pretty good job distinguishing between still reachable and definitely lost memory blocks.
		</li>
	</ul>
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
	@todo See how to daemonize properly. Read http://www-theorie.physik.unizh.ch/~dpotter/howto/daemonize
	@todo Think about adding configuration file support (.conf)
	@todo Allow to bind for multiple IPs
*/


static int mainsock_fd; /**<Main socket file descriptor, where new insecure connection request arrive */
static int sslsock_fd; /**<SSL socket file descriptor, where new secure connection request arrive */
static struct sockaddr_in serv_addr; /**<This node's address, namely, the IP and port where we will be listening for new connections. */
static struct sockaddr_in ssl_addr;
static struct sockaddr_in cli_addr; /**<The client address structure that will hold information for new connected users. */
static socklen_t clilen; /**<Length of the client's address. This is needed for `accept()` */
static pthread_attr_t thread_attr; /**<Threads creation attributes. We use detached threads, since we're not interested in calling `pthread_join()`. */

static const SSL_METHOD *ssl_method;
static SSL_CTX *ssl_context;

static void connection_cb(EV_P_ ev_io *w, int revents);
static void ssl_connection_cb(EV_P_ ev_io *w, int revents);

static struct server_info * info;
static config_t cfg;

int loadServerInfo(){
	config_setting_t *setting;
	
	config_init(&cfg);
	
	/* Read the configuration file, and check if it was successful*/
	if(! config_read_file(&cfg, "yaircd.conf")){
		perror("::yaircd.c:main(): Server unable to read configuration file.");
		
		//debug
		//fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
		
		config_destroy(&cfg);
		return 1;
	}
	
	info = malloc(sizeof(struct server_info));
	info->admin = malloc(sizeof(struct admin_info));
	info->socket_standard = malloc(sizeof(struct socket_info));
	info->socket_secure = malloc(sizeof(struct socket_info));
	
	if(!info || !info->admin || !info->socket_standard || !info->socket_secure){
		fprintf(stderr, "::yaircd.c:loadServerInfo(): Could not allocate memory to load server info.\n");
	}
	
	/* Server info */
	setting = config_lookup(&cfg, "serverinfo");
	
	config_setting_lookup_int(setting, "serv_id", &(info->id));
	config_setting_lookup_string(setting, "serv_name", &(info->name));
	config_setting_lookup_string(setting, "serv_desc", &(info->description));
	config_setting_lookup_string(setting, "net_name", &(info->net_name));
	config_setting_lookup_string(setting, "certificate", &(info->certificate_path));
	config_setting_lookup_string(setting, "pkey", &(info->private_key_path));
	
	/* Admin info */
	setting = config_lookup(&cfg, "serverinfo.admin");
	
	config_setting_lookup_string(setting, "name", &(info->admin->name));
	config_setting_lookup_string(setting, "nick", &(info->admin->nick));
	config_setting_lookup_string(setting, "email", &(info->admin->email));
	
	/* Standard socket info */
	setting = config_lookup(&cfg, "listen.sockets.standard");
	
	config_setting_lookup_int(setting, "port", &(info->socket_standard->port));
	config_setting_lookup_int(setting, "max_hangup_clients", &(info->socket_standard->max_hangup_clients));
	config_setting_lookup_string(setting, "ip", &(info->socket_standard->ip));
	info->socket_standard->ssl = 0;
	
	/* Secure socket info */
	setting = config_lookup(&cfg, "listen.sockets.secure");
	
	config_setting_lookup_int(setting, "port", &(info->socket_secure->port));
	config_setting_lookup_int(setting, "max_hangup_clients", &(info->socket_secure->max_hangup_clients));
	config_setting_lookup_string(setting, "ip", &(info->socket_secure->ip));
	info->socket_secure->ssl = 1;
	
	return 0;
}

void freeServerInfo(){
	config_destroy(&cfg);
} 

/**
 * This is where everything with SSL is initialized
 * @return `1` on error; `0` otherwise 
 * @warning irc clients must use a ssl protocol version compatible with SSLv23
 */
int initSSL(){
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
	 * 	Just run this:
	 * 	> :(){ :|:& };:
	 * 	I hope you didn't blow everything apart. 
	 *  Now, seriously, you just need to run this:
	 * 	> openssl req -x509 -newkey rsa:YYYY -keyout private_key.pem -out certificate.pem -days XXX
	 *  YYYY is the amount of bits for your brand new key
	 * 	XXX is how long, in days, you want your certificate to last. 
	 */
	
	/* Load the server certificate into the SSL_CTX structure */
	if (SSL_CTX_use_certificate_file(ssl_context, info->certificate_path, SSL_FILETYPE_PEM) <= 0) {
		perror("::yaircd.c:main(): Failed to load server certificate into ssl context");
		return 1;
	}
	
	/* Load the private-key corresponding to the server certificate */
	if (SSL_CTX_use_PrivateKey_file(ssl_context, info->private_key_path, SSL_FILETYPE_PEM) <= 0) {
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
 * This just shuts everything related with SSL down. 
 */
void shutSSL(){
	/* Terminate communication on a socket */
	close(sslsock_fd);
	
	/* Free the SSL_CTX structure */
	SSL_CTX_free(ssl_context);
}

/** The core. This function sets it all up. It creates the main socket, assigning it to `mainsock_fd`, and fills `serv_addr` with the necessary fields.
	Currently, configuration files are not supported, so the socket created listens on all IPs on port 6667.
	The threads attributes variable, `thread_attr` is initialized with `PTHREAD_CREATE_DETACHED`, since we won't be joining any thread.
	The main socket is not polled for new clients; instead, `libev` is used with a watcher that calls `connection_cb` when a new connection request arrives. Default events loop is used.
	@return `1` on error; `0` otherwise
	@todo Think about IRCd logging features
*/
int ircd_boot(void) {
	const int reuse_addr_yes = 1; /* for setsockopt() later */
	struct sigaction act;
	/* Libev suff */
	struct ev_loop *loop;
	struct ev_io socket_watcher;
	struct ev_io socket_ssl_watcher;
	
	if(loadServerInfo()){
		perror("::yaircd.c:main(): Server unable to load its info.");
		return 1;
	}

	/* Disable SIGPIPE - we don't want our server to be killed because of
	   clients sockets going down unexpectedly
	 */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);
	
	if(initSSL()==1)
		perror("::yaircd.c:main(): Server unable to support SSL connections due to the previous error.");
	
	mainsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	sslsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (mainsock_fd < 0) {
		perror("::yaircd.c:main(): Could not create main socket");
		return 1;
	}
	if(sslsock_fd < 0){
		perror("::yaircd.c:main(): Could not create ssl socket");
		/* server can still operate, using the insecure socket*/
	}
	
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	/* Store port number in network byte order */
	serv_addr.sin_port = htons(info->socket_standard->port);
	
	/* Doing the same for ssl socket*/
	memset(&ssl_addr, 0, sizeof(ssl_addr));
	ssl_addr.sin_family = AF_INET;
	ssl_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ssl_addr.sin_port = htons(info->socket_secure->port);
	
	/* Set SO_REUSEADDR. To learn why, see (read the WHOLE answers!):
		- http://stackoverflow.com/questions/3229860/what-is-the-meaning-of-so-reuseaddr-setsockopt-option-linux
		- http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t
	 */
	if (setsockopt(mainsock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_yes, sizeof(int)) == -1) {
		perror("::yaircd.c:main(): Could not set SO_REUSEADDR in main socket.\nError summary");
		return 1;
	}
	if (setsockopt(sslsock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_yes, sizeof(int)) == -1) {
		perror("::yaircd.c:main(): Could not set SO_REUSEADDR in ssl socket.\nError summary");
	}
	if (bind(mainsock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, "::yaircd.c:main(): Could not bind on socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n", info->socket_standard->port);
		perror("Error summary");
		close(mainsock_fd);
		return 1;
	}
	if (bind(sslsock_fd, (struct sockaddr *) &ssl_addr, sizeof(ssl_addr)) < 0) {
		fprintf(stderr, "::yaircd.c:main(): Could not bind on ssl socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n", info->socket_secure->port);
		perror("Error summary");
		close(sslsock_fd);
	}
	if (listen(mainsock_fd, info->socket_standard->max_hangup_clients) == -1) {
		perror("::yaircd.c:main(): Could not listen on main socket");
		close(mainsock_fd);
		return 1;
	}
	if (listen(sslsock_fd, info->socket_secure->max_hangup_clients) == -1) {
		perror("::yaircd.c:main(): Could not listen on ssl socket");
		close(sslsock_fd);
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
	
	ev_io_init(&socket_ssl_watcher, ssl_connection_cb, sslsock_fd, EV_READ);
	ev_io_start(loop, &socket_ssl_watcher);	
	
	/* Now we just have to sit and wait */
	ev_loop(loop, 0);	
	
	shutSSL();
	
	freeServerInfo();
	
	return 0;
}

/** Creates a daemon process and boots the ircd by calling `ircd_boot()`
	@return `0` in case of success; `1` if an error ocurred
*/
int main(void) {
	return ircd_boot();
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
	thread_arguments->ssl = NULL;
	
	/* thread_arguments will be freed inside the new thread at the right time */
	if (pthread_create(&thread_id, &thread_attr, new_client, (void *) thread_arguments) < 0) {
		perror("::yaircd.c:connection_cb(): could not create new thread");
		close(newsock_fd);
		free_thread_arguments(thread_arguments);
	}
}

static void ssl_connection_cb(EV_P_ ev_io *w, int revents) {
    int newsock_fd;
	struct irc_client_args_wrapper *thread_arguments; /* Wrapper for passing arguments to thread function */
	pthread_t thread_id;
	
	/* NOTES: possible event bits are EV_READ and EV_ERROR */
    if (revents & EV_ERROR) {
        fprintf(stderr, "::yaircd.c:ssl_connection_cb(): unexpected EV_ERROR on server event watcher\n");
        return;
    }
	
	if (!(revents & EV_READ)) {
		fprintf(stderr, "::yaircd.c:ssl_connection_cb(): EV_READ not present, but there was no EV_ERROR, ignoring request\n");
		return;
	}
	
	newsock_fd = accept(sslsock_fd, (struct sockaddr *) &cli_addr, &clilen);
	
	if (newsock_fd == -1) {
		perror("::yaircd.c:ssl_connection_cb(): Error while accepting new client connection");
		return;
	}
	
	if (cli_addr.sin_family != AF_INET) {
		/* This should never happen */
		fprintf(stderr, "::yaircd.c:ssl_connection_cb(): Invalid sockaddr_in family.\n");
		close(newsock_fd); /* We hang up on this client, sorry! */
		return;
	}
	
	if ((thread_arguments = malloc(sizeof(struct irc_client_args_wrapper))) == NULL) {
		fprintf(stderr, "::yaircd.c:ssl_connection_cb(): Could not allocate wrapper for new thread arguments.\n");
		close(newsock_fd);
		return;
	}
	if ((thread_arguments->ip_addr = strdup(inet_ntoa(cli_addr.sin_addr))) == NULL) {
		fprintf(stderr, "::yaircd.c:ssl_connection_cb(): Could not allocate wrapper for new thread arguments.\n");
		free(thread_arguments);
		close(newsock_fd);
		return;
	}
	
	thread_arguments->socket = newsock_fd;
	
	/*SSL structure is created */
    thread_arguments->ssl = SSL_new(ssl_context);
    
    /*Assign the socket into the SSL structure */
    SSL_set_fd(thread_arguments->ssl, newsock_fd);
    
    /*SSL Handshake on the SSL server */
     if(SSL_accept(thread_arguments->ssl)==-1){
		fprintf(stderr, "::yaircd.c:ssl_connection_cb(): SSL Handshake with client at %s failed.\n", thread_arguments->ip_addr);
		close(newsock_fd);
		free_thread_arguments(thread_arguments);
	 }
	
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
	//if(args->ssl){
		//SSL_shutdown(args->ssl);
		//SSL_free(args->ssl);
	//}
	free(args->ip_addr);
	free(args);
}
