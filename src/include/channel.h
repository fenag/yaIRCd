#ifndef __YAIRCD_CHANNEL_GUARD__
#define __YAIRCD_CHANNEL_GUARD__

/** Returned by `do_join()` when there isn't enough memory to create a new channel */
#define CHAN_NO_MEM 1

/** Returned by `do_join()` when a user tried to create a channel with an invalid name */
#define CHAN_INVALID_NAME 2

/** Opaque type for a channelused by the rest of the code */
typedef struct irc_channel *irc_channel_ptr;

/* Documented in .c source file */
int chan_init(void);
void chan_destroy(void);
int do_join(struct irc_client *client, char *channel);

#endif /* __YAIRCD_CHANNEL_GUARD__ */
