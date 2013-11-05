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
#include "client/client.h"
#define SOCK_MAX_HANGUP_CLIENTS 3

static int mainsock_fd;
static socklen_t clilen;
static struct sockaddr_in serv_addr;
static struct sockaddr_in cli_addr;
static pthread_attr_t thread_attr;

static void connection_cb(EV_P_ ev_io *w, int revents);

int main(int argc, char *argv[]) {
	
	int portno = 6667; /* TODO put this in configuration file options */
	
	/* Libev stuff */
	struct ev_loop *loop;
	struct ev_io socket_watcher;

	mainsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (mainsock_fd < 0) {
		perror("::yaircd.c:main(): Could not create main socket");
		return 1;
	}
	
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* TODO add configuration file support to bind to multiple IPs */
	
	/* Store port number in network byte order */
	serv_addr.sin_port = htons(portno);
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
	
	/* TODO Figure out a better spot to place these */
	pthread_attr_destroy(&thread_attr);
	ev_loop_destroy(loop);
	close(mainsock_fd);
	return 0;
}

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
	
	/* TODO - think about ircd logging features
	   log("*** Client connected");
	*/
	
	if (cli_addr.sin_family != AF_INET) {
		/* This should never happen */
		fprintf(stderr, "::yaircd.c:connection_cb(): Invalid sockaddr_in family.\n");
		close(newsock_fd); /* We hang up on this client, sorry! */
		return;
	}
	
	thread_arguments = malloc(sizeof(struct irc_client_args_wrapper));
	thread_arguments->socket = newsock_fd;
	thread_arguments->ip_addr = strdup(inet_ntoa(cli_addr.sin_addr));
	
	/* thread_arguments will be free()'d inside new_client() */
	
	if (pthread_create(&thread_id, &thread_attr, new_client, (void *) thread_arguments) < 0) {
		perror("::yaircd.c:connection_cb(): could not create new thread");
		close(newsock_fd);
		return;
	}
}
