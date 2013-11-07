#include "client/client.h"
#include "client/client_list.h"
#include "protocol/limits.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ev.h>

/** @file
	@brief Implementation of functions that deal with irc clients

	These functions provide an abstraction layer to deal with irc clients. A client is defined to be anything connecting to the server that is not a server.
	Every operation to be performed in an `irc_client` shall be invoked through a function defined in this file.
	
	@author Filipe Goncalves
	@date November 2013
*/

char n[] = "a"; /* TEMP - nick generator */

static void manage_client_messages(EV_P_ ev_io *watcher, int revents);
static int new_client_connection(char *ip_addr, int socket);
static void destroy_client(struct irc_client *client);
static void free_client(struct irc_client *client);

/* Documented in header file client.h */
void *new_client(void *args) {
	struct irc_client_args_wrapper *arguments = (struct irc_client_args_wrapper *) args;
	int sockfd;
	char *ip;
	
	sockfd = arguments->socket;
	ip = strdup(arguments->ip_addr);
	free(args);
	
	if (new_client_connection(ip, sockfd) == 0) {
		fprintf(stderr, "::client.c:new_client(): Could not allocate memory for new client\n");
		free(ip);
	}
	
	return NULL;
}

/** The core function that deals with a client. This is the callback function for a client's connection (previously set by `new_client_connection()`). It is automagically called everytime something
	fresh and interesting to read arrives at the client's socket, or everytime connection to this client was lost for some reason (process died silently, TCP connection broken, etc.).
	It parses and interprets the client's message according to the official RFC, and sends a reply (if there is one to send, some commands do not require a reply).
	This function can return prematurely if, for some reason, `EV_ERROR` was reported by `libev` or if `EV_READ` flag is not set (meaning there is no data to read from the socket, which should never happen because
	the watcher is for detecting new data that has just arrived), in which case an appropriate error message is printed.
	In the case that the client disconnected from the server (either because the connection was closed abruptly, or voluntarily by issuing a QUIT command), `destroy_client()` is called, and the appropriate quit message
	is spread around the network.
	Otherwise, it calls `notify_all_clients()` to spread the new message around the network.
	@param watcher The watcher that brought this callback function to life. This argument is safely casted to `struct irc_client *`, since the watcher field is the first in `struct irc_client`. This is safe and it
	works because the watcher is embedded inside each client's struct.
	@param revents Flags provided by `libev` describing what happened. In case of `EV_ERROR`, or if `EV_READ` is not set, the function prints an error message and returns prematurely.
*/
static void manage_client_messages(EV_P_ ev_io *watcher, int revents) {
	char msg_buf[MAX_MSG_SIZE];
	int msg_size;
	struct irc_client *client;
	char *tmp; /* TEMPORARY - workaround for notify_all_clients after destroying a session */
	
    if (revents & EV_ERROR) {
        fprintf(stderr, "::client.c:manage_client_messages(): unexpected EV_ERROR on client event watcher\n");
        return;
    }
	if (!(revents & EV_READ)) {
		fprintf(stderr, "::client.c:manage_client_messages(): EV_READ not present, but there was no EV_ERROR, ignoring message\n");
		return;
	}
	
	client = (struct irc_client *) watcher;
	
	msg_size = read(client->socket_fd, msg_buf, sizeof(msg_buf));
	
	if (msg_size == 0) {
		/* Broken pipe - client process ended abruptly */
		tmp = strdup(client->nick);
		destroy_client(client);
		notify_all_clients("[QUITTING - Broken Pipe]\n", tmp, 25);
		free(tmp);
		return;
	}
	
	if (msg_size == -1) {
		perror("::client.c:manage_client_messages(): error while attempting to read from socket");
		tmp = strdup(client->nick);
		destroy_client(client);
		notify_all_clients("[QUITTING - Fatal error]\n", tmp, 25);
		free(tmp);
		return;
	}
	
	if (msg_buf[0] == 'q') {
		notify_all_clients("[QUITTING]\n", client->nick, 11);
		destroy_client(client);
		return;
	}
	
	notify_all_clients(msg_buf, client->nick, msg_size);
}

/** This is the function that gets called by `new_client()` to create a new client instance. It allocs memory for a new `irc_client` instance and initializes every field, including `ev_watcher` and `ev_loop`.
	`ev_loop` is a regular loop created with `ev_loop_new(0)`.
	`ev_watcher` is initialized with `manage_client_messages()` as a callback function.
	Note that, as a consequence, every client's thread will hold a loop of its own to manage this client's network I/O.
	It also adds the new client to the list of known connected users.
	After greeting the new user, it begins execution of the events loop. Thus, this function will not return until the loop is broken, which is done by `manage_client_messages()`. When that happens, the loop is
	destroyed, and every resource that was alloced for this client is `free()`'d.
	@param ip_addr A characters sequence describing the client's IP address. IPv4 only.
	@param socket socket file descriptor for the new client.
	@return `0` if no resources are available to register this client;
			`1` if everything worked smoothly
*/
static int new_client_connection(char *ip_addr, int socket) {

	struct irc_client *new_client;
	char temp[MAX_MSG_SIZE];
	
	if ((new_client = malloc(sizeof(struct irc_client))) == NULL)
		return 0;
		
	new_client->realname = "Just a test";
	new_client->hostname = ip_addr;
	new_client->nick = malloc(2);
	strcpy(new_client->nick, n);
	n[0]++;
	new_client->username = "developer";
	new_client->server = "development.yaircd.org";
	new_client->socket_fd = socket;
	
	new_client->ev_loop = ev_loop_new(0);
	ev_io_init(&new_client->ev_watcher, manage_client_messages, socket, EV_READ);
	ev_io_start(new_client->ev_loop, &new_client->ev_watcher);

	client_list_add(new_client);
	
	sprintf(temp, "Hello. I am a basic IRC Server, take it easy on me!\n"
	"Your full identification for me is: %s!%s@%s\n"
	"You are connecting from: %s\n"
	"Your nickname is: %s\n"
	"Your realname is: %s\n"
	"Your username is: %s\n"
	"You are on server: %s\n"
	"Type in your message. Enjoy your chatting session!\n",
	new_client->nick, new_client->username, new_client->hostname, new_client->hostname, new_client->nick, new_client->realname, new_client->username, new_client->server);

	write(new_client->socket_fd, temp, strlen(temp));
	
	/* Wait for messages coming from client */
	ev_run(new_client->ev_loop, 0);
	
	/* When we get here, the loop was broken by destroy_client(), we can now free it */
	ev_loop_destroy(new_client->ev_loop);
	
	free(new_client);
	
	return 1;
}

/** Called everytime connection is lost with a client, either because it was abruptly closed, or the client manually issued a QUIT command.
	It deletes the client from the list of connected clients, closes the socket and frees his resources with the exception of the pointer itself (`client`), and
	the event loop. We can't free the event loop, since we're doing work inside a callback function. Instead, we stop the loop, and let control go back to `new_client_connection()` to
	destroy the loop object.
	@param client The client who got disconnected.
*/
static void destroy_client(struct irc_client *client) {
	client_list_delete(client);
	free_client(client);
}

/** Auxiliary function called by `destroy_client()` to free a client's resources.
	It frees every dynamic alloced resource, closes the socket, stops the callback mechanism (detaches the watcher from the events loop), and breaks this client's
	ev_loop. Control should return back to `new_client_connection()` after the loop is broken.
	As a consequence, this function does not free the client pointer itself, nor does it destroy the event loop.
	@param client The client to free
*/
static void free_client(struct irc_client *client) {
	/*free(client->realname);*/ /* TODO - remove this comment when client->realname is not a constant string anymore */
	free(client->hostname);
	free(client->nick);
	/*free(client->username);*/
	/*ree(client->server);*/ /* TODO think about whether this is really necessary */
	close(client->socket_fd);
	
	ev_io_stop(client->ev_loop, &client->ev_watcher); /* Stop the callback mechanism */
	ev_break(client->ev_loop, EVBREAK_ONE); /* Stop iterating */
}
