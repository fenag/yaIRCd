#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <stdio.h>
#include "cloak.h"
#include "serverinfo.h"

/** @file
   @brief Hosts cloaking library implementation and algorithm explanation
   This file implements a set of funtions that know how to cloak a user's host. Host cloaking consists of encoding a
   user's host into a hashed value such that channel bans with (or without) wildcards still work, but it is
   impossible for regular users to know other people's IP address. Thus, a hash that still allows wildcard matching
   is necessary.
   Credits for this algorithm go to UnrealIRCd's team. The algorithm is taken from the cloak module (src/modules/cloak.c
   inside Unreal source). However, we use SHA1 and MD5 instead of relying solely on MD5. We do this because MD5 is known
   to have been broken, and SHA1 too, but there are no successfull attacks to BOTH hashes when they are used simultaneously.
   The algorithm uses 3 keys as salt for the hash functions.
   These keys should remain secret, since they are used as salt for the hash, that is, they create some
   more confusion around the value being hashed, making it harder for an attacker to brute force a cloaked host by
   iterating over every possible IP. Choosing strong, long keys with random entries should make it nearly impossible
   to use a brute force approach. Every server in a network must use the same set of keys.
   These keys shall be random and include upper case, lower case and digits, with a length of at least 5 characters and
   at most 100.
   The resulting hash value depends on whether a user's IP was successfully reverse looked up. If we have a valid
   reverse hostname for a user, the cloaked host will be the value of evaluating
   `downsample(md5(sha1(KEY1:host:KEY2)+KEY3))` and prepending the net prefix, where `+` means string concatenation
   and `downsample()` is a custom hash function that trims down a 128-bit MD5 hash into an unsigned integer, later
   printed using hexadecimal notation.
   We then concatenate this value with that part of the hostname that is common to other users on the same subnet, that is,
   anything after the first dot separator.
   For example, if we have the host `bl-11-252-66-85.dsl.telepac.pt`, the cloaked host will be
   `downsample(md5(sha1(KEY1:bl-11-252-66-85.dsl.telepac.pt:KEY2)+KEY3)).dsl.telepac.pt`. The downsampled entry should
    be unique,thus allowing for a specific user ban in a channel. As a convention, we also prepend cloaked hosts with
    the net prefix and a hyphen. The net prefix is configured in the server's conf file, it's just a regular string
    describing the network that will be prefixed to cloaked hosts coming from reverse looked up hostnames. Thus,
    assuming the net prefix is "ex", in our example, the final result would be
    `ex-downsample(md5(sha1(KEY1:bl-11-252-66-85.dsl.telepac.pt:KEY2)+KEY3)).dsl.telepac.pt`. Because the result of
    evaluating `downsample()` is printed as a hexadecimal number, a possible result would be:
    `ex-B700B718.dsl.telepac.pt`.
   The common part of the host is assumed to be everything after the first dot, which in this case is `.dsl.telepac.pt`.
   Including this allows for range bans like `*!*@*.dsl.telepac.pt`, or `*!*@*.telepac.pt`, or even `*!*@*.pt`. Note
   that `*!*@*.pt` will ban every user with a reverse looked up hostname from Portugal, but portuguese users that
   don't have reverse lookup are still able to join.
   If there isn't a common hostname part (e.g., the user's hostname is `localhost`, or any other host for which there
   are no dot separators), then the cloaked host won't include any common part. For example, anyone connecting from
   `localhost` will hold a cloaked host similar to: ´ex-C4FEA00B`.
   If a reverse lookup did not yield any results, the algorithm works directly with the IP address.
   If it's an IPv4 address, then it matches the general form `A.B.C.D`. We define 3 values, `alpha`, `beta` and `gamma`,
   such that `alpha = downsample(md5(sha1("KEY2:A.B.C.D:KEY3")+"KEY1"))`,
   `beta = downsample(md5(sha1("KEY3:A.B.C:KEY1")+"KEY2"))`, and `gamma = downsample(md5(sha1("KEY1:A.B:KEY2")+"KEY3"))`. 
   Thus, `alpha` is unique for `A.B.C.D`, `beta` is unique for `A.B.C.*`, and `gamma` is unique for `A.B.*.*`. The resulting
   cloaked host is then given by `alpha.beta.gamma.IP`. For example, the IP `85.224.201.217` can be masked as
   `5A008DC5.3300AB76.850082D1.IP`.
   IPv6 cloaking is still not supported in yaIRCd, but the algorithm will work as follows:
   <ul>
   <li>ALPHA = downsample(md5(sha1("KEY2:a:b:c:d:e:f:g:h:KEY3")+"KEY1"))</li>
   <li>BETA  = downsample(md5(sha1("KEY3:a:b:c:d:e:f:g:KEY1")+"KEY2"))</li>
   <li>GAMMA = downsample(md5(sha1("KEY1:a:b:c:d:KEY2")+"KEY3"))</li>
   </ul>
   And the cloaked host will be given by `ALPHA:BETA:GAMMA:IP`.
   Special care must be taken, since IPv6 addresses do not necessarily hold every field `a-h`. For example, `localhost`
   can be written using `::1`. This still needs some discussion, but a possible solution is to expand every IPv6 into
   a unified form where fields `a-h` can always be matched, and then use the above method safely.
   @author Filipe Goncalves
   @date December 2013
 */

/** How many bits a hexadecimal number holds */
#define BITS_IN_HEXA 4

/** Maximum length of a cloaked host corresponding to a reverse looked up hostname. Used as buffer size for
   `hide_host()` */
#define MAX_HOST_LEN 128

/** Takes 3 salt keys and a text, and stores `md5(sha1(salt1+":"+text+":"+salt2)+salt3)` into `result`.
   @param salt1 The key used as a salt to prepend to `":"+text+":"`. Does not have to be null terminated.
   @param salt2 The key used as a salt to append to `":"+text+":"`. Does not have to be null terminated.
   @param salt3 The key used as a salt to append to `sha1(salt1+":"text+":"+salt2)`. Does not have to be null terminated.
   @param salt1_len `salt1` length, excluding any possible null terminating character
   @param salt2_len `salt2` length, excluding any possible null terminating character
   @param salt3_len `salt3` length, excluding any possible null terminating character
   @param text A pointer to a characters sequence holding the text that shall be joined to the salt keys. This sequence
			   does not have to be null terminated.
   @param text_len `text` length, excluding any possible null terminating character
   @param result A pointer to a valid and allocated memory location capable of holding at least `MD5_DIGEST_LENGTH`
				(defined in `openssl/md5.h`) characters, and where the result of evaluating
				`md5(sha1(salt1+":"text+":"+salt2)+salt3)` is stored. The resulting sequence is not null terminated.
   @note Upon returning, `result` will hold exactly `MD5_DIGEST_LENGTH` characters.
   @warning `result` is not null terminated.
   @warning `result` shall be a valid and allocated memory location.
 */
static void do_md5(const char *salt1,
		   size_t salt1_len,
		   const char *salt2,
		   size_t salt2_len,
		   const char *salt3,
		   size_t salt3_len,
		   const char *text,
		   size_t text_len,
		   unsigned char result[MD5_DIGEST_LENGTH])
{
	char buf1[salt1_len + salt2_len + text_len + 2]; /* +2 because we need space for two ':' */
	char buf2[SHA_DIGEST_LENGTH + salt3_len];
	strncpy(buf1, salt1, salt1_len);
	buf1[salt1_len] = ':';
	strncpy(buf1 + salt1_len + 1, text, text_len);
	buf1[salt1_len + text_len + 1] = ':';
	strncpy(buf1 + salt1_len + text_len + 2, salt2, salt2_len);
	SHA1((const unsigned char*)buf1, (unsigned long) sizeof(buf1), (unsigned char*)buf2);
	strncpy(buf2 + SHA_DIGEST_LENGTH, salt3, salt3_len);
	MD5((const unsigned char*)buf2, sizeof(buf2), result);
}

/** Packs an MD5 hash consisting of `MD5_DIGEST_LENGTH` bytes into a singe integer.
   To do so, we check how many integers we would need to hold a hash of `MD5_DIGEST_LENGTH` bytes, which is given by
   `MD5_DIGEST_LENGTH/sizeof(unsigned int)`. We then break an integer into chunks of 
   `(sizeof(unsigned int)*CHAR_BIT)/(MD5_DIGEST_LENGTH/sizeof(unsigned int))` bits, that is, if an integer holds `N` bits,
   and `M` integers would be needed to store an MD5 hash, then we will just divide our `N` bits by `M`, so that
   we get equal slots that hold a piece of the hash information. In other words, it's like we packed
   `(MD5_DIGEST_LENGTH/sizeof(unsigned int))` into a single integer.
   As a consequence, each chunk will hold the result of zipping `sizeof(unsigned int)` bytes from the hash value.
   However, since `N/M` is obviously less bits than `sizeof(unsigned int)*CHAR_BIT`, we will need to pack groups of
   `sizeof(unsigned int)` bytes into `N/M` bits. This is done by XORing `sizeof(unsigned int)` adjacent bytes from
   the hash, and storing the result in `N/M` bits.
   As we move forward in the algorithm, a mask is kept to shift the result of the current iteration's XOR into the next
   byte inside an integer. Bytes are filled from most significant to least significant.
   It might be good to run this through a practical example and see the actual numbers. Typically, `CHAR_BIT` is 8 bits,
   integers are 32 bits, and MD5 hash values are 128-bit. Thus, to store a hash of 128-bit, we would need `128/32 = 4`
   integers. Thus, we grab the 32 bits that compose an integer, and evenly divide them by `4`, yielding `32/4 = 8`,
   that is, 8 bits will store the hash of 4 bytes (because the size of an integer is 4 bytes). As a consequence, each
   iteration will pick the next 4 bytes from the hash, XOR them together, and store them in the current byte position
   from our integer.
   @param hash A pointer to a valid and allocated memory location capable of holding at least `MD5_DIGEST_LENGTH`
			   characters, where the hash is stored.
   @return An unsigned integer equivalent to the downsampled hash.
 */
static unsigned int downsample(unsigned char hash[MD5_DIGEST_LENGTH])
{
	size_t int_len, mask_step, mask;
	size_t i, j;
	unsigned sample;
	unsigned char tmp;
	int_len = (size_t)(sizeof(unsigned int) * CHAR_BIT);
	mask_step = (size_t)(int_len / (MD5_DIGEST_LENGTH / sizeof(unsigned int)));
	mask = (size_t)(int_len - mask_step);
	for (sample = 0, i = 0; i < MD5_DIGEST_LENGTH; i += sizeof(unsigned int), mask -= mask_step) {
		for (tmp = 0, j = 0; j < sizeof(unsigned int); j++) {
			tmp ^= hash[i + j];
		}
		sample = (sample | tmp) << mask;
	}
	return sample;
}

/** Knows how to hide an IPv4 address. See this file's description for further details on the algorithm.
   @param host A pointer to a null terminated characters sequence denoting the user's ip address. Should be a string of
			   the form "A.B.C.D".
   @return A dynamically allocated pointer to a null terminated characters sequence holding the cloaked host for this
		   user. If there isn't enough memory available, `NULL` is returned.
	@note The caller is responsible for freeing the returned pointer.
 */
char *hide_ipv4(char *host)
{
	unsigned char alpha[MD5_DIGEST_LENGTH];
	unsigned char beta[MD5_DIGEST_LENGTH];
	unsigned char gamma[MD5_DIGEST_LENGTH];
	char result[(CHAR_BIT / BITS_IN_HEXA) * sizeof(unsigned) * 3 + 6];
	size_t len;

	len = strlen(host);
	do_md5(get_cloak_key(2), get_cloak_key_length(2), get_cloak_key(3), get_cloak_key_length(3), get_cloak_key(
		       1), get_cloak_key_length(1), host, len, alpha);
	for (len--; host[len] != '.'; len--)
		;  /* Intentionally left blank */
	/* assert: host[len] == '.' */
	do_md5(get_cloak_key(3), get_cloak_key_length(3), get_cloak_key(1), get_cloak_key_length(1), get_cloak_key(
		       2), get_cloak_key_length(2), host, len, beta);
	for (len--; host[len] != '.'; len--)
		;  /* Intentionally left blank */
	do_md5(get_cloak_key(1), get_cloak_key_length(1), get_cloak_key(2), get_cloak_key_length(2), get_cloak_key(
		       3), get_cloak_key_length(3), host, len, gamma);
	sprintf(result, "%X.%X.%X.IP", downsample(alpha), downsample(beta), downsample(gamma));
	return strdup(result);
}

/** Knows how to hide a reverse looked up address. See this file's description for further details on the algorithm.
   @param host A pointer to a null terminated characters sequence denoting the user's hostname.
   @return A dynamically allocated pointer to a null terminated characters sequence holding the cloaked host for this
           user. If there isn't enough memory available, `NULL` is returned. 
   @note The caller is responsible for freeing the returned pointer.
 */
char *hide_host(char *host)
{
	unsigned char alpha[MD5_DIGEST_LENGTH];
	char *p;
	char result[MAX_HOST_LEN];
	size_t host_len = strlen(host);

	do_md5(get_cloak_key(1), get_cloak_key_length(1), get_cloak_key(2), get_cloak_key_length(2), get_cloak_key(
		       3), get_cloak_key_length(3), host, host_len, alpha);
	for (p = host; *p != '\0' && (*p != '.' || !isalpha((unsigned char)*(p + 1))); p++)
		;  /* Intentionally left blank */
	snprintf(result, sizeof(result), "%s-%X%s", get_cloak_net_prefix(), downsample(alpha), *p == '\0' ? "" : p);
	return strdup(result);
}
