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
#include <string.h>
#include "client/client.h"
#define SOCK_MAX_HANGUP_CLIENTS 3

int main(int argc, char *argv[]) {
	int mainsock_fd;
	int newsock_fd;
	int portno = 50001; /* TODO put this in configuration file options */
	socklen_t clilen;
	
	pthread_t thread_id;
	pthread_attr_t thread_attr;
	
	/* Structs describing server and client addresses */
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	
	/* Wrapper for passing arguments to thread function 
	   Defined in client/client.h
	 */
	struct irc_client_args_wrapper *thread_arguments;
	
	mainsock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (mainsock_fd < 0) {
		perror("::main(): Could not create main socket");
		return 1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* TODO add configuration file support to bind to multiple IPs */
	
	/* Store port number in network byte order */
	serv_addr.sin_port = htons(portno);
	if (bind(mainsock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, "::main(): Could not bind on socket with port %d. Please make sure this port is free, and that the IP you're binding to is valid.\n", portno);
		perror("Error summary");
		return 1;
	}
	if (listen(mainsock_fd, SOCK_MAX_HANGUP_CLIENTS) == -1) {
		perror("::main(): Could not listen on main socket");
		return 1;
	}
	clilen = sizeof(cli_addr);
	
	/* Initialize thread creation attributes */
	if (pthread_attr_init(&thread_attr) != 0) {
		/* On Linux, this will never happen */
		perror("::main(): Could not initialize thread attributes");
		return 1;
	}
	
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	
	/* This is the server's main loop. It accepts new client connections and creates a thread for each one */
	while (1) {
		newsock_fd = accept(mainsock_fd, (struct sockaddr *) &cli_addr, &clilen);
		if (newsock_fd == -1) {
			perror("::main(): Error while accepting new client connection");
			continue;
		}
		if (cli_addr.sin_family != AF_INET) {
			/* This should never happen */
			fprintf(stderr, "::main(): Invalid sockaddr_in family.\n");
			close(newsock_fd); /* We hang up on this client, sorry! */
			continue;
		}
		
		thread_arguments = malloc(sizeof(struct irc_client_args_wrapper));
		thread_arguments->socket = newsock_fd;
		thread_arguments->ip_addr = strdup(inet_ntoa(cli_addr.sin_addr));
		
		/* thread_arguments will be free()'d inside new_client() */
		
		if (pthread_create(&thread_id, &thread_attr, new_client, (void *) thread_arguments) < 0) {
			perror("::main(): could not create new thread");
			return 1;
		}
	}
	
	/* TODO Figure out a better place to put these */
	pthread_attr_destroy(&thread_attr);
	close(mainsock_fd);
	
	return 0;
}
