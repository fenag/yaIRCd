#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ev.h>
#include "client.h"
#include "client_list.h"
#include "protocol.h"
#include "wrappers.h"
#include "parsemsg.h"

/** @file
	@brief Implementation of functions that deal with irc clients

	These functions provide an abstraction layer to deal with irc clients. A client is defined to be anything connecting to the server that is not a server.
	Every operation to be performed in an `irc_client` shall be invoked through a function defined in this file.
	
	@author Filipe Goncalves
	@date November 2013
	@todo Implement timeouts
	@todo Move client message exchanching routines to another file
	@todo Implement IRC commands :)
*/

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

/** Temp. This has to be rewritten */
static void print_err_reply(struct irc_client *client, numreply_t reply) {
	char str[4];
	sprintf(str, NUMREPLY_T_FORMAT, reply);
	write(client->socket_fd, str, (size_t) 3);
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
	@todo Handle optional PASS command in connection registration. Discuss what should happen when a PASS command is issued, but the server does not require a password.
	@todo Implement ERR_NICKNAMEINUSE, ERR_ERRONEUSNICKNAME, and ERR_NICKCOLLISION
*/
static void manage_client_messages(EV_P_ ev_io *watcher, int revents) {
	char msg_in[MAX_MSG_SIZE+1];
	ssize_t msg_size;
	struct irc_client *client;
	/* Stuff needed to parse message */
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
		fprintf(stderr, "::client.c:manage_client_messages(): EV_READ not present, but there was no EV_ERROR, ignoring message\n");
		return;
	}
	client = (struct irc_client *) watcher;
	msg_size = read(client->socket_fd, msg_in, (size_t) MAX_MSG_SIZE);
	if (msg_size == 0) {
		/* Broken pipe - client process ended abruptly */
		destroy_client(client);
		return;
	}
	if (msg_size == -1) {
		perror("::client.c:manage_client_messages(): error while attempting to read from socket");
		destroy_client(client);
		return;
	}
	/* assert: msg_size > 0 && msg_size <= MAX_MSG_SIZE
	   It is safe to write to msg_in[msg_size] since we have space for MAX_MSG_SIZE+1 chars
	*/
	msg_in[msg_size] = '\0'; /* Ensures the message is null-terminated, as required by parsemsg() */
	parse_res = parse_msg(msg_in, msg_size, &prefix, &cmd, params, &params_no);
	
	if (client->is_registered == 0) {
		if (parse_res == -1) {
			/* RFC Section 4.1: until the connection is registered, the server must respond to any non-registration commands with ERR_NOTREGISTERED */
			print_err_reply(client, ERR_NOTREGISTERED);
			return;
		}
		else if (strcasecmp(cmd, ==, "nick")) {
			/* TODO Implement ERR_NICKNAMEINUSE, ERR_ERRONEUSNICKNAME, and ERR_NICKCOLLISION */
			if (params_no < 1) {
				print_err_reply(client, ERR_NONICKNAMEGIVEN);
				return;
			}
			else {
				client->nick = strdup(params[0]);
			}
		}
		else if (strcasecmp(cmd, ==, "user")) {
			if (params_no < 4) {
				print_err_reply(client, ERR_NEEDMOREPARAMS);
				return;
			}
			else {
				client->username = strdup(params[0]);
				client->realname = strdup(params[3]);
			}
		}
		else {
			print_err_reply(client, ERR_NOTREGISTERED);
			return;
		}
		if (client->nick != NULL && client->username != NULL && client->realname != NULL) {
			client->is_registered = 1;
		}
	}
	else {
		if (msg_in[0] == 'q') {
			notify_all_clients("[QUITTING]\n", client->nick, 11);
			destroy_client(client);
			return;
		}
		/* This is old crap, god knows what others will get after calling parsemg()... TODO fix this */
		notify_all_clients(msg_in, client->nick, msg_size);
	}
}

/** This is the function that gets called by `new_client()` to create a new client instance. It allocs memory for a new `irc_client` instance and initializes the `hostname`, `socket_fd`, `ev_loop` and `ev_watcher` fields.
	`ev_loop` is a regular loop created with `ev_loop_new(0)`.
	`ev_watcher` is initialized with `manage_client_messages()` as a callback function.
	Note that, as a consequence, every client's thread will hold a loop of its own to manage this client's network I/O.
	It also adds the new client to the list of known connected users.
	After initializing the new user, it begins execution of the events loop. Thus, this function will not return until the loop is broken, which is done by `manage_client_messages()`. When that happens, the loop is
	destroyed, and every resource that was alloced for this client is `free()`'d.
	@param ip_addr A characters sequence describing the client's IP address. IPv4 only.
	@param socket socket file descriptor for the new client.
	@return `0` if no resources are available to register this client;
			`1` if everything worked smoothly
*/
static int new_client_connection(char *ip_addr, int socket) {
	struct irc_client *new_client;
	if ((new_client = malloc(sizeof(struct irc_client))) == NULL)
		return 0;

	new_client->hostname = ip_addr;
	new_client->socket_fd = socket;
	new_client->ev_loop = ev_loop_new(0);
	new_client->server = NULL; /* local client */
	new_client->is_registered = 0;
	
	/* Initialize unknown information - wait for client registration */
	new_client->realname = NULL;
	new_client->nick = NULL;
	new_client->username = NULL;
	
	ev_io_init(&new_client->ev_watcher, manage_client_messages, socket, EV_READ);
	ev_io_start(new_client->ev_loop, &new_client->ev_watcher);

	client_list_add(new_client);
	
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
	free(client->realname);
	free(client->hostname);
	free(client->nick);
	free(client->username);
	free(client->server);
	close(client->socket_fd);
	ev_io_stop(client->ev_loop, &client->ev_watcher); /* Stop the callback mechanism */
	ev_break(client->ev_loop, EVBREAK_ONE); /* Stop iterating */
}
