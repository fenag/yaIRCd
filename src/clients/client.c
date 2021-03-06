#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ev.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "client.h"
#include "client_list.h"
#include "protocol.h"
#include "wrappers.h"
#include "parsemsg.h"
#include "msgio.h"
#include "interpretmsg.h"
#include "cloak.h"
#include "serverinfo.h"
#include "read_msgs.h"
#include "send_err.h"
#include "channel.h"

/** @file
   @brief Implementation of functions that deal with irc clients
   These functions provide an abstraction layer to deal with irc clients. A client is defined to be anything connecting
      to the server that is not a server.
   Every operation to be performed in an `irc_client` shall be invoked through a function defined in this file.
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
 */

static void manage_client_messages(EV_P_ ev_io *watcher, int revents);
void destroy_client(void *arg);
static void free_client(struct irc_client *client);
static struct irc_client *create_client(struct irc_client_args_wrapper *args);
void free_thread_arguments(struct irc_client_args_wrapper *);
static void queue_async_cb(EV_P_ ev_async *w, int revents);
static void ping_timer_cb(EV_P_ ev_timer *w, int revents);

/** Accepts a new client's connection. This function is indirectly called by the threads scheduler. When a new client
   pops in, the main process allocates a new thread whose init function is this one.
   This function creates a new client instance and starts an event loop for this client.
   @param args A pointer to `struct irc_client_args_wrapper`, casted to `void `. It is assumed that it points to an
      address in heap. This is always casted to `struct irc_client_args_wrapper `.
   This parameter is `free()`'d when it is not needed anymore; the caller does not need to worry about freeing the
      memory.
   We chose heap allocation here because otherwise there would be a possible race condition: after the main process
      calls `pthread_create()`, it is undefined which instruction is executed next (the next instructions inside the
      main process, or the thread's init routine). In the unfortunate case that the main process kept executing and a
      new client immediately arrived, there was a chance that `irc_client_args_wrapper` in the main's process stack
      would be updated with the new client's values before the thread execution for this client started. Therefore, the
      main process explicitly allocs memoryto pass arguments for each new client that arrives.
   @return This function always returns `NULL`
 */
void *new_client(void *args)
{
	struct irc_client *client;
	if ((client = create_client((struct irc_client_args_wrapper*)args)) == NULL) {
		return NULL;
	}
	pthread_cleanup_push(destroy_client, (void*)client);
	/* At this point, we have:
	        - A client structure successfully allocated
	        - An events loop and 4 watchers - IO watcher, async watcher, and timers for PING
	        - A thread's cleanup handler to exit gracefully
	   Let the party begin!
	 */
	ev_io_init(&client->io_watcher, manage_client_messages, client->socket_fd, EV_READ);
	ev_io_start(client->ev_loop, &client->io_watcher);
	ev_async_init(&client->async_watcher, queue_async_cb);
	ev_async_start(client->ev_loop, &client->async_watcher);
	client->last_activity = ev_now(client->ev_loop);
	ev_init(&client->time_watcher, ping_timer_cb);
	ping_timer_cb(client->ev_loop, &client->time_watcher, 0);
	ev_run(client->ev_loop, 0); /* Go */

	/* This is never reached, but we need to pair up push() and pop() calls */
	pthread_cleanup_pop(0);

	return NULL;
}

/** The core function that deals with a client. This is the callback function for a client's connection (previously set
   by `new_client_connection()`). It is automagically called everytime somethingfresh and interesting to read arrives at
   the client's socket, or everytime connection to this client was lost for some reason (process died silently, TCP
   connection broken, etc.).
   It parses and interprets the client's message according to the official RFC, and sends a reply (if there is one to
      send, some commands do not require a reply).
   This function can return prematurely if, for some reason, `EV_ERROR` was reported by `libev` or if `EV_READ` flag is
      not set (meaning there is no data to read from the socket, which should never happen becausethe watcher is for
      detecting new data that has just arrived), in which case an appropriate error message is printed.
   In the case that the client disconnected from the server (either because the connection was closed abruptly, or
      voluntarily by issuing a QUIT command), `destroy_client()` is called, and the appropriate quit messageis spread
      around the network.
   Otherwise, it calls `notify_all_clients()` to spread the new message around the network.
   @param watcher The watcher that brought this callback function to life. This argument is safely casted to `struct
      irc_client `, since the watcher field is the first in `struct irc_client`. This is safe and itworks because the
      watcher is embedded inside each client's struct.
   @param revents Flags provided by `libev` describing what happened. In case of `EV_ERROR`, or if `EV_READ` is not set,
      the function prints an error message and returns prematurely.
   @todo Handle optional PASS command in connection registration. Discuss what should happen when a PASS command is
      issued, but the server does not require a password.
   @todo Implement ERR_NICKNAMEINUSE, ERR_ERRONEUSNICKNAME, and ERR_NICKCOLLISION
   @todo Notify other clients when a client disconnects abruptly
 */
static void manage_client_messages(EV_P_ ev_io *watcher, int revents)
{
	struct irc_client *client;
	char *msg_in;
	int msg_size;
	int params_no;
	int parse_res;
	char *prefix;
	char *cmd;
	char *params[MAX_IRC_PARAMS];

	if (revents & EV_ERROR) {
		fprintf(stderr, "::client.c:manage_client_messages(): unexpected EV_ERROR on client event watcher\n");
		return;
	}
	if (!(revents & EV_READ)) {
		fprintf(
			stderr,
			"::client.c:manage_client_messages(): EV_READ not present, but there was no EV_ERROR, ignoring message\n");
		return;
	}
	client = (struct irc_client*)((char*)watcher - offsetof(struct irc_client, io_watcher));

	read_data(client);

	while ((msg_size = next_msg(&client->last_msg, &msg_in)) != MSG_CONTINUE) {
		if (msg_size == 0 || (msg_size == 1 && msg_in[msg_size - 1] == '\r')) {
			/* Silently ignore empty messages */
			printf("EMPTY MSG\n");
			continue;
		}
		/* Handle clients which terminate messages with \n and clients that use \r\n */
		if (msg_size >= 1 && msg_in[msg_size - 1] == '\r') {
			msg_in[msg_size - 1] = '\0';
		} else {
			msg_in[msg_size] = '\0';
		}
		printf("Got new message: %s\n", msg_in);
		parse_res = parse_msg(msg_in, &prefix, &cmd, params, &params_no);
		if (parse_res == -1) {
			send_err_unknowncommand(client, "");
			continue;
		}
		interpret_msg(client, prefix, cmd, params, params_no);
	}
}

/** Creates a new client instance that will be used throughout this client's lifetime.
   @param args A pre-filled arguments wrapper with appropriate information. See the documentation for `struct
      irc_client_args_wrapper` for further information.
   @return `NULL` if there aren't enough resources to create a new client; otherwise, pointer to `struct irc_client` for
      this user.
 */
static struct irc_client *create_client(struct irc_client_args_wrapper *args)
{
	struct irc_client *new_client;
	char hostbuf[NI_MAXHOST];
	char ip[INET_ADDRSTRLEN];
	/* In the future
	   char ip6[INET6_ADDRSTRLEN];
	 */
	if ((new_client = malloc(sizeof(struct irc_client))) == NULL) {
		free_thread_arguments(args);
		return NULL;
	}
	if ((new_client->ev_loop = ev_loop_new(0)) == NULL) {
		free(new_client);
		free_thread_arguments(args);
		return NULL;
	}
	if (client_queue_init(&new_client->write_queue) == -1) {
		ev_loop_destroy(new_client->ev_loop);
		free(new_client);
		free_thread_arguments(args);
		return NULL;
	}
	if ((new_client->channels = calloc((size_t) get_chanlimit(), sizeof(*new_client->channels))) == NULL) {
		client_queue_destroy(&new_client->write_queue);
		ev_loop_destroy(new_client->ev_loop);
		free(new_client);
		free_thread_arguments(args);
	}
	new_client->socket_fd = args->socket;
	new_client->server = NULL; /* local client */
	new_client->is_registered = 0;
	new_client->uses_ssl = (args->ssl != NULL);
	new_client->ssl = args->ssl;
	new_client->realname = NULL;
	new_client->nick = NULL;
	new_client->username = NULL;
	new_client->hostname = NULL;
	new_client->public_host = NULL;
	new_client->channels_count = 0;
	new_client->connection_status = STATUS_OK;
	initialize_irc_message(&new_client->last_msg);

	yaircd_send(new_client, ":%s NOTICE AUTH :*** Looking up your hostname...\r\n", get_server_name());
	if (!args->is_ipv6) {
		if (getnameinfo((struct sockaddr*)&args->address.ipv4_address, args->address_length, hostbuf,
				sizeof(hostbuf), NULL, 0, NI_NAMEREQD) != 0) {
			yaircd_send(
				new_client,
				":%s NOTICE AUTH :*** Couldn't resolve your hostname; using your IP address instead.\r\n",
				get_server_name());
			if (inet_ntop(AF_INET, (void*)&args->address.ipv4_address.sin_addr, ip, sizeof(ip)) == NULL) {
				/* Weird case ... no reverse lookup, and invalid IP..? */
				fprintf(
					stderr,
					"::client.c:create_client(): Couldn't find a reverse hostname, and inet_ntop() reported an error.\n");
				ev_loop_destroy(new_client->ev_loop);
				free(new_client);
				free_thread_arguments(args);
				return NULL;
			}
			new_client->host_reversed = 0;
			if ((new_client->hostname = strdup(ip)) == NULL) {
				ev_loop_destroy(new_client->ev_loop);
				free(new_client);
				free_thread_arguments(args);
				return NULL;
			}
		}else {
			yaircd_send(new_client, ":%s NOTICE AUTH :*** Found your hostname.\r\n", get_server_name());
			new_client->host_reversed = 1;
			if ((new_client->hostname = strdup(hostbuf)) == NULL) {
				ev_loop_destroy(new_client->ev_loop);
				free(new_client);
				free_thread_arguments(args);
				return NULL;
			}
		}
		if ((new_client->public_host = (new_client->host_reversed ? hide_host(new_client->hostname) : hide_ipv4(new_client->hostname))) == NULL) {
			free(new_client->hostname);
			ev_loop_destroy(new_client->ev_loop);
			free(new_client);
			free_thread_arguments(args);
			return NULL;
		}
	}
	free_thread_arguments(args);
	return new_client;
}

/** Callback function for a client's async watcher.
   This function is called by libev when another thread issues `async_send()` on this thread's async watcher.
   We use this mechanism to notify client threads that new data is queued, waiting to be written into the socket.
   For example, if a thread from client A reads a PRIVMSG command with a message whose destination is B, then A's thread
      willqueue the message into B's queue, and call `async_send()` on B's async watcher, to wake B up. When B wakes up,
      this function is called.
   Therefore, the main purpose of this function is to flush a client's queue.
   @param w Pointer to this client's async watcher. A pointer to the client is obtained with `(struct irc_client *) ((char *)w - offsetof(struct irc_client, async_watcher))`. 
			This pointer manipulation is necessary to extract the client's structure where `w` is embedded. In doubt, read about `offsetof()` macro in `stddef.h`'s manpage.
   @param revents libev's flags. Not used for async callbacks.
 */
static void queue_async_cb(EV_P_ ev_async *w, int revents)
{
	struct irc_client *client;
	client = (struct irc_client*)((char*)w - offsetof(struct irc_client, async_watcher));
	flush_queue(client, &client->write_queue);
}

/** Called by the rest of the code everytime a client's session must be terminated. The reason for terminating a
	client's session can be a voluntary QUIT, broken pipe, socket read or write error, connection lost, ping
	timeout; in other words, any condition that makes it impossible to communicate to this client.
	The code calling this function shall provide a proper quit message, depending on where the situation arised.
	See `protocol.h` to learn some possible, built-in quit messages.
	Either way, this function starts by trying to notify the client about this action, using the `ERROR` command,
	as described in the protocol. The exact syntax of the message sent to the client follows this form:
	`ERROR :Closing Link: &lt;nick&gt;[&lt;hostname&gt;] (&lt;quit message&gt;)`.
	Whether the write is successfull or not is irrelevant, after attempting to notify the client about this,
	the function calls `do_quit()`, to let every other client sharing a channel with this one that he's leaving,
	and finally, `pthread_exit()` is called to free every resource allocated to this client and kill the thread.
	@param client The client to disconnect.
	@param quit_msg The quit message. This must be a valid pointer to a null-terminated characters sequence with
	the quit message. Since no `free()`'s are performed on this parameter, it must NOT be a dynamically allocated pointer.
	Typically, this will be a pointer to a string constant defined in `protocol.h` if the event triggering the QUIT
	was an error on the server side. Otherwise, it is ok for this parameter to be a pointer to a local variable
	stored in the stack of the calling function, as is the case with `cmd_quit()` in `interpretmsg.c`.
	@note No other place in the code should call `pthread_exit()`. Always use this function to terminate a client's
	session.
*/
void terminate_session(struct irc_client *client, char *quit_msg) {
	int size;
	char err_msg[MAX_MSG_SIZE+1];
	size = cmd_print_reply(err_msg, sizeof(err_msg), "ERROR :Closing Link: %s[%s] (%s)\r\n",
				(client->is_registered ? client->nick : "*"), client->hostname, quit_msg);
	(void) write_to(client, err_msg, size);
	do_quit(client, quit_msg);
	pthread_exit(NULL); /* Calls destroy_client() */
}

/** Callback function used by the time watcher for each client.
	Alright now, listen up, this is important. We have carefully followed libev's documentation suggestions, and implemented
	the 3rd method described in this section: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#code_ev_timer_code_relative_and_opti
	We use a timer for each client, stored in the client's structure, and also a connection_status bit-field. See the definition for the client's
	structure in `client.h` for further information.
	The basic layout goes like this: We let the timer expire every `get_ping_freq()` seconds. When that happens, we check up on this client, using
	his `last_activity` field, which is updated by `read_data()` (see `read_msgs.c`) everytime new data arrives to the socket. `last_activity` is a time
	stamp holding the last known time when a message from this client arrived from the socket. If the difference between the current time and `last_activity`
	is greater than `get_ping_freq()`, then `get_ping_freq()` seconds have elapsed since the last period of activity, and it is time to send a PING message.
	When we send a PING message, this client's connection state is switched to `STATUS_TIMEOUT`, meaning we are waiting for a PONG reply. As a consequence,
	the timer is set to expire after `get_timeout()` seconds, which is typically much lower than `get_ping_freq()` (around 10 secs., for example). Thus, the next
	time we enter the callback function, using `connection_status`, we can know if we have sent a ping to this client before. If that is the case, and the timeout
	time has elapsed, then it means we didn't get a PONG (or any other message) reply, and we assume this connection is dead. `terminate_session()` is called, and
	the client is removed from the server.
	In any other case, we just reset back the timer to expire after `get_ping_freq()` seconds, and the whole cycle repeats.
   @param w Pointer to this client's time watcher. A pointer to the client is obtained with `(struct irc_client *) ((char *)w - offsetof(struct irc_client, time_watcher))`. 
			This pointer manipulation is necessary to extract the client's structure where `w` is embedded. In doubt, read about `offsetof()` macro in `stddef.h`'s manpage.
   @param revents libev's flags. Not used for this specific callback.
*/
static void ping_timer_cb(EV_P_ ev_timer *w, int revents) {
	char ping_msg[MAX_MSG_SIZE+1];
	struct irc_client *client;
	int size;
	ev_tstamp after;
	client = (struct irc_client*)((char*)w - offsetof(struct irc_client, time_watcher));
    after = client->last_activity - ev_now(client->ev_loop) + get_ping_freq();
	if (after < 0.) {
		if (client->connection_status == STATUS_OK) {
			/* Hey, you there? */
			size = cmd_print_reply(ping_msg, sizeof(ping_msg), "PING :%s\r\n", get_server_name());
			client->connection_status = STATUS_TIMEOUT;
			(void) write_to(client, ping_msg, (size_t) size);
			ev_timer_set(w, get_timeout(), 0.);
			ev_timer_start(client->ev_loop, w);
		}
		else {
			/* Oops! */
			terminate_session(client, TIMEOUT_QUIT_MSG);
		}
	}
    else {
		/* There was recent activity, reset timer */
		ev_timer_set(w, get_ping_freq(), 0.);
        ev_timer_start(client->ev_loop, w);
    }
}

/** This function is set by the thread init function (`new_client()`) as the cleanup handler for `pthread_exit()`, thus,
   this is called when a fatal error with this client occurred andhe needs to be kicked out of the server.
   Examples of fatal errors are: we were writing to his socket and processing a command he sent and suddenly the
      connection was lost, causing write() to return an error; there's no space in client_list for this user; or
      something else went terribly wrong and we can't keep a connection to this user.
   The socket is closed, every watcher is stopped, the events loop is destroyed, the client is deleted from the client's
      list (if his connection was registered), and every resource associated with this client is freed.
   @param arg A pointer to a `struct irc_client` describing this client. This argument is always casted to `struct
      irc_client `.
   @warning This function is only active after we have a fully allocated client struct for this user. Problems with
      structure allocation are dealt earlier in `new_client()`.
   @warning Care must be taken when killing a client. For example, deleting a client from the list can be problematic.
      Clients list implementation is thread safe; consider the case that this thread was doing some processing and was
      currently holding a lock to the clients list when a fatal error occurs, `pthread_exit()` is called, and we end up
      in this function. This function would try to delete the client from the list, trying to obtain the lock again,
      which would result in a deadlock, since the thread would be waiting for itself. Although it is a rare case, it is
      extremely undesirable, therefore, abstract implementations used by each client's threaddo not call
      `pthread_exit()` directly. Furthermore, imagine another example where we just killed the thread - what if this
      thread was currently holding a lock to a shared list? This lock won't be released, and from now on,no other thread
      will ever be able to access this shared resource.
   This is why library functions such as `client_list_add()` and others use special return values or parameters to
      indicate failure, so that the client's thread can decide to call `pthread_exit()` after making sure
      everysynchronization mechanism is unlocked.
   @note You may have wondered if closing the socket is safe, since we don't really know what happened: the socket can
      be invalid by this time, and closing it can yield an error. According to `close()` manpage, "Not checking the
      return value of `close()` is a common but nevertheless serious programming error. It is quite possible that errors
      on a previous `write()` operation are first reported at the final `close()`. Not checking the return value when
      closing the file may lead to silent loss of data. This can especially be observed with NFS and with disk quota."
      We think this is great advice, but is not very applicable to sockets. We're killing this client anyway, why bother
      with some final errors on his socket? Thus, the return value for `close()` is ignored.
   @note Every exiting path for a client ends up his thread with `pthread_exit()`. This destructor is always called when
      the client exits the server. Due to `pthread_exit()`'s nature, we don't actually ever returnback to
      `new_client()`.
   @todo Notify other clients when someone leaves.
 */
void destroy_client(void *arg)
{
	struct irc_client *client = (struct irc_client*)arg;
	/* First, we HAVE to delete this client from the clients list, no matter what.
	   Why? Because if we delete him, we know for sure that no other thread will be able to reach him and
	   issue client_enqueue() commands on this guy. List accesses are thread safe; other clients using the list to
	      perform
	   enqueue operations do so atomically. Thus, after client_list_delete() is completed, no other thread will ever
	      try to access
	   this client's queue. This is required by client_queue_destroy(), see the documentation in client_queue.c
	 */
	if (client->is_registered) {
		client_list_delete(client);
	}
	free_client(client);
}

/** Auxiliary function called by `destroy_client()` to free a client's resources.
   It frees every dynamic allocated resource, closes the socket, stops the callback mechanism by detaching the watcher
      from the events loop, and destroys this client'sev_loop.
   @param client The client to free
   @todo find the way to free client->ssl
 */
static void free_client(struct irc_client *client)
{
	/* Some fields, such as client->realname, can be NULL if the client is not registered.
	   This is not a problem though: free(NULL) is defined and is safe to call, according to
	   the POSIX specification.
	 */
	free(client->realname);
	free(client->hostname);
	free(client->nick);
	free(client->username);
	free(client->server);
	free(client->public_host);
	free(client->channels);
	if (client_queue_destroy(&client->write_queue) == -1) {
		fprintf(stderr, "Warning: client_queue_destroy() reported an error - THIS SHOULD NEVER HAPPEN!\n");
	}
	//if(client->ssl == NULL){
	//SSL_shutdown(client->ssl);
	//SSL_free(client->ssl);
	//}
	close(client->socket_fd);

	/* Stop the callback mechanism for this client */
	ev_io_stop(client->ev_loop, &client->io_watcher);
	ev_async_stop(client->ev_loop, &client->async_watcher);
	ev_timer_stop(client->ev_loop, &client->time_watcher);
	
	ev_break(client->ev_loop, EVBREAK_ONE); /* Stop iterating */
	ev_loop_destroy(client->ev_loop);
	free(client);
}
