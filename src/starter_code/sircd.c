/* To compile: gcc sircd.c rtlib.c rtgrading.c csapp.c -lpthread -osircd */

#include "rtlib.h"
#include "rtgrading.h"
#include "csapp.h"
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>

/* Macros */
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 512

/* Global variables */
u_long curr_nodeID;
rt_config_file_t   curr_node_config_file;  /* The config_file  for this node */
rt_config_entry_t *curr_node_config_entry; /* The config_entry for this node */

/* Function prototypes */
void init_node( int argc, char *argv[] );
size_t get_msg( char *buf, char *msg );
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] );

/* Main */
int main( int argc, char *argv[] )
{
    init_node( argc, argv );
    int listen =  Open_listenfd(curr_node_config_entr->irc_port);
    if (listen < 0) {
    	return -1;
    }
    printf( "I am node %d and I listen on port %d for new users\n", curr_nodeID, curr_node_config_entry->irc_port );
    /* 
     * Initialize client data structure
     * client_db = client_structure_new();
     */
    /* 
     * Look at making re-entrant functions; what makes a function threadsafe? 
     * 		- ask billy 
     * Design the client datastructure.
     * Implement a library to do that
     */
    while(true) {
    	/* Add Client FD's From data structure to FD_SET  and calc nfds*/
	 	/* 
		 * int fd_list[] = client_structure_getfds(client_db); 
		 * FD_ZERO(&readset);
		 * for (int i = 0; i < sizeof(fd_list); i++) {
		 * 	nfds = max(fd_list[i],nfds);
		 * 	FD_SET(fd_list[i],&readset);
		 * }
		 */
	/* Call select */
		/* 
		 * ready = select(nfds,&readset,&writeset,&excptset,null); 
		 */
	/* Respond to select */
		/* Could this be done in a separate thread? */
		/* Find the readable FD's */
			/* Read from the FD */
				/* Recv into buffer */
			/* Parse command and Generate Response */
				/* While buffer not empt call get_msg */
				/* Interprage msg* and generate response */
			/* Send response */
				/* call Send */
			/* Update Clients data structure as nessacerry */
				/* Serialized XML for data structure parsing? */
				/* Linked List or Hashmap? */
					/* 14-213 */
    }
    return 0;
}

/*
 * void init_node( int argc, char *argv[] )
 *
 * Takes care of initializing a node for an IRC server
 * from the given command line arguments
 */
void init_node( int argc, char *argv[] )
{
    int i;

    if( argc < 3 )
    {
        printf( "%s <nodeID> <config file>\n", argv[0] );
        exit( 0 );
    }

    /* Parse nodeID */
    curr_nodeID = atol( argv[1] );

    /* Store  */
    rt_parse_config_file(argv[0], &curr_node_config_file, argv[2] );

    /* Get config file for this node */
    for( i = 0; i < curr_node_config_file.size; ++i )
        if( curr_node_config_file.entries[i].nodeID == curr_nodeID )
             curr_node_config_entry = &curr_node_config_file.entries[i];

    /* Check to see if nodeID is valid */
    if( !curr_node_config_entry )
    {
        printf( "Invalid NodeID\n" );
        exit(1);
    }
}


/*
 * size_t get_msg( char *buf, char *msg )
 *
 * char *buf : the buffer containing the text to be parsed
 * char *msg : a user malloc'ed buffer to which get_msg will copy the message
 *
 * Copies all the characters from buf[0] up to and including the first instance
 * of the IRC endline characters "\r\n" into msg.  msg should be at least as
 * large as buf to prevent overflow.
 *
 * Returns the size of the message copied to msg.
 */
size_t get_msg(char *buf, char *msg)
{
    char *end;
    int  len;

    /* Find end of message */
    end = strstr(buf, "\r\n");

    if( end )
    {
        len = end - buf + 2;
    }
    else
    {
        /* Could not find \r\n, try searching only for \n */
        end = strstr(buf, "\n");
	if( end )
	    len = end - buf + 1;
	else
	    return -1;
    }

    /* found a complete message */
    memcpy(msg, buf, len);
    msg[end-buf] = '\0';

    return len;	
}

/*
 * int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
 *
 * A strtok() variant.  If in_buf is a space-separated list of words,
 * then on return tokens[X] will contain the Xth word in in_buf.
 *
 * Note: You might want to look at the first word in tokens to
 * determine what action to take next.
 *
 * Returns the number of tokens parsed.
 */
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1] )
{
    int i = 0;
    const char *current = in_buf;
    int  done = 0;

    /* Possible Bug: handling of too many args */
    while (!done && (i<MAX_MSG_TOKENS)) {
        char *next = strchr(current, ' ');

	if (next) {
	    memcpy(tokens[i], current, next-current);
	    tokens[i][next-current] = '\0';
	    current = next + 1;   /* move over the space */
	    ++i;

	    /* trailing token */
	    if (*current == ':') {
	        ++current;
		strcpy(tokens[i], current);
		++i;
		done = 1;
	    }
	} else {
	    strcpy(tokens[i], current);
	    ++i;
	    done = 1;
	}
    }

    return i;
}
