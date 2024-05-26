#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
generate_random_string(char *str, size_t size)
{
	const char charset[] =
	    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	if (size) {
		--size;
		for (size_t n = 0; n < size; n++) {
			int key = rand() % (int)(sizeof charset - 1);
			str[n] = charset[key];
		}
		str[size] = '\0';
	}
}

int
main(int argc, char **argv)
{
	unsigned int j = 0;
	redisContext *c;
	redisReply *reply;

	const char *hostname = "127.0.0.1";
	int port = 6379;

	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
	c = redisConnectWithTimeout(hostname, port, timeout);
	if (c == NULL || c->err) {
		if (c) {
			printf("Connection error: %s\n", c->errstr);
			redisFree(c);
		} else {
			printf(
			    "Connection error: can't allocate redis context\n");
		}
		exit(1);
	}

	char key[20], value[1000]; // Modify sizes as needed

	while (1) {
		generate_random_string(key, sizeof(key));
		generate_random_string(value, sizeof(value));
		reply = redisCommand(c, "SET %s %s", key, value);
		freeReplyObject(reply);
		reply = redisCommand(c, "GET %s", key);
		freeReplyObject(reply);

		j++;
		if (j % 1000 == 0) {
			printf("Completed %u operations\n", j);
		}
	}

	// Disconnect and free the context
	redisFree(c);
	return 0;
}
