#include <pthread.h>
#include <limits.h>
#include "trie.h"
#include "list.h"
#include "client_list.h"
#include "channel.h"
#include "msgio.h"

/** A constant denoting the number of different characters allowed in a channel name.
	Since only 7 characters are not allowed, we chose to map every character and include this
	rule in `is_valid()`. 
*/
#define CHANNEL_ALPHABET_SIZE (UCHAR_MAX+1)

/** This structure represents a channel user. We will store instances of this structure in a trie associated to each nick in the channel */
struct chan_user {
	unsigned modes; /**<This user's status in the channel */
	struct irc_client *user; /**<Pointer to this user's client structure */
};

/** This structure represents an IRC channel
	The IRCd keeps a thread-safe list of channels associating each channel name to a node holding this structure.
*/
struct irc_channel {
	char *name; /**<Null terminated characters sequence holding the channel name */
	char *topic; /**<Channel topic */
	struct trie_t *users; /**<List of users on this channel */
	int users_count; /**<How many users are in the channel */
	unsigned modes; /**<Channel modes */
};

/** An arguments wrapper for `list_find_and_execute()` functions. */
struct irc_channel_wrapper {
	Word_list_ptr chan_list;
	struct irc_client *client; /**<Original client where the request came from */
	char *channel; /**<Channel name */
};

/**Global channels list for the whole network */
Word_list_ptr channels;

/** Defines valid characters for a channel name. As of this writing, the protocol allows any character except NUL, BELL, CR, LF, SPACE, COMMA and SEMI-COLLON.
	@param c The character to analyze
	@return `1` if `c` is allowed in a channel name; `0` otherwise.
*/
static int is_valid(char c) {
	return c != '\0' && c != '\a' && c != '\r' && c != '\n' && c != ' ' && c != ',' && c != ':';
}

/** Maps a character ID into the corresponding character. Since we allow nearly any character, this is a direct mapping
	@param i The character ID
	@return Corresponding character whose ID is `i`
*/
static char pos_to_char(int i) {
	return (char) i;
}

/** Maps a character into a unique ID. Since we allow nearly any character, this is a direct mapping
	@param c The character
	@return `c`'s unique ID.
*/
static int char_to_pos(char c) {
	return (int) (unsigned char) c;
}

/** Initializes the channels module.
	@return `0` on success; `-1` on failure. `-1` indicates a resources allocation error.
	@warning This function must be called exactly once, by the parent thread, before any thread is created and tries to access any channel.
 */
int chan_init(void) {
	if ((channels = init_word_list(NULL, is_valid, pos_to_char, char_to_pos, CHANNEL_ALPHABET_SIZE)) == NULL) {
		return -1;
	}
	return 0;
}

/** Destroys every channel.
	@warning This function must be called exactly once, by the parent thread, after every thread is dead and no more accesses to the channels will be performed.
 */
void chan_destroy(void) {
	destroy_word_list(channels, LIST_NO_FREE_NODE_DATA);
}

static void join_ack(struct irc_client *client, irc_channel_ptr chan) {
	char msg[MAX_MSG_SIZE+1];
	char nick[MAX_NICK_LENGTH+1];
	int size;
	int err_code;
	struct trie_node_stack *search_info;
	void *user;
	struct chan_user *chanuser;
	
	size = cmd_print_reply(msg, sizeof(msg), 
		":%s!%s@%s JOIN :%s\r\n", client->nick, client->username, client->public_host, chan->name);
	(void) write_to(client, msg, size);
	size = cmd_print_reply(msg, sizeof(msg),
		":%s MODE %s +nt\r\n", "development.yaircd.org", chan->name);
	(void) write_to(client, msg, size);
	size = cmd_print_reply(msg, sizeof(msg),
		":%s " RPL_TOPIC " %s %s :%s\r\n", "development.yaircd.org", client->nick, chan->name, chan->topic);
	(void) write_to(client, msg, size);
	
	for (search_info = find_by_prefix_next_trie(chan->users, NULL, "", MAX_NICK_LENGTH+1, nick, &err_code, &user);
		 search_info != NULL;
		 search_info = find_by_prefix_next_trie(chan->users, search_info, "", MAX_NICK_LENGTH+1, nick, &err_code, &user)) {
		if (err_code == TRIE_NO_MEM) {
			fprintf(stderr, "::channel.c:join_ack(): TRIE_NO_MEM occurred when listing channel users.\n");
			continue;
		}
		chanuser = (struct chan_user *) user;
		size = cmd_print_reply(msg, sizeof(msg),
			":%s " RPL_NAMREPLY " %s = %s :%s!%s@%s\r\n", "development.yaircd.org", client->nick, chan->name, chanuser->user->nick, chanuser->user->username, chanuser->user->public_host);
		(void) write_to(client, msg, size);
	}
	
	size = cmd_print_reply(msg, sizeof(msg),
		":%s " RPL_ENDOFNAMES " %s %s :End of NAMES list\r\n", "development.yaircd.org", client->nick, chan->name);
	(void) write_to(client, msg, size);
}

static void *join_newchan(void *args) {
	struct irc_channel_wrapper *info;
	irc_channel_ptr new_chan;
	struct chan_user *new_user;
	
	info = (struct irc_channel_wrapper *) args;	
	if ((new_chan = malloc(sizeof(*new_chan))) == NULL || (new_user = malloc(sizeof(*new_user))) == NULL) {
		return NULL;
	}
	if ((new_chan->name = strdup(info->channel)) == NULL) {
		free(new_chan);
		free(new_user);
		return NULL;
	}
	if ((new_chan->users = init_trie(NULL, nick_is_valid, nick_pos_to_char, nick_char_to_pos, NICK_EDGES_NO)) == NULL) {
		free(new_chan->name);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	if (list_add_nolock(info->chan_list, new_chan, info->channel) == LST_NO_MEM) {
		destroy_trie(new_chan->users, TRIE_NO_FREE_DATA, NULL);
		free(new_chan->name);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	new_user->modes = 0;
	new_user->user = info->client;
	if (add_word_trie(new_chan->users, info->client->nick, (void *) new_user) == TRIE_NO_MEM) {
		free(new_chan->name);
		list_delete_nolock(info->chan_list, info->channel);
		destroy_trie(new_chan->users, TRIE_NO_FREE_DATA, NULL);
		free(new_chan);
		free(new_user);
		return NULL;
	}
	new_chan->users_count = 1;
	new_chan->modes = 0;
	new_chan->topic = "This is an example channel";
	join_ack(info->client, new_chan);
	return (void *) new_chan;
}

int do_join(struct irc_client *client, char *channel) {
	/* TODO - Check if channel name is valid */
	struct irc_channel_wrapper args;
	void *ret;
	int result;
	
	args.chan_list = channels;
	args.client = client;
	args.channel = channel;
	ret = list_find_and_execute_globalock(channels, channel, NULL, join_newchan, NULL, (void *) &args, &result);
	if (ret == NULL) {
		return CHAN_NO_MEM;
	}
	return 0;
}
