#ifndef _CLIENT_LIST_H_
#define _CLIENT_LIST_H_
#include <stdio.h>

/* 
 * Add()
 * Remove_By_Name()
 * Remove_By_FD()
 * Get_By_Name()
 * Get_By_FD()
 * Get_By_Hostname()
 * Modify()
 * Print()
 * Iterator()
 */

struct client_s {
	char *r_name;  //realname
	char *h_name;  //hostname
	char *nick;    //nickname
	char *username; //username
	char *con_point; //connection point/server name
	int fd;
};

struct node_s { 
	client_t client;
	client_t *next;
};

typedef struct node_s node_t;
typedef struct client_s client_t;
/*Client Linked List functions*/
node_t cl_new(realname,hostname,nick,username,connection_point);
/*Add an element to the list*/
int cl_add();
/*Remove using the nickname as the param for the client*/
int cl_remove_by_name();
/*remove using the FD as the param for the client*/
int cl_remove_by_fd();
/*Get the client struct using the name as the param*/
int cl_get_by_name();
/*Get the client struct using the fd as the param*/
int cl_get_by_fd();
/*get hte client struct using hte hostname as the param*/
int cl_get_by_hostname();
/*Print out the client list*/
int cl_print();
/*Convert the client list into a string*/
char *cl_tostr();
int *cl_getfds();
/*Iterator for client list*/
client_t *cl_iterator();
/* Moves the iterator from its current position in the list to he next elem*/
client_t *cl_iter_next(client_t*);
client_t *cl_iter_has_next(client_t*);
#endif
