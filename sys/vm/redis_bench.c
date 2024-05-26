#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sys/systm.h"

#define KEY_SIZE 20
#define VALUE_SIZE 1000 // 根据需要调整，模拟较大的英文文本
#define TOTAL_OPERATIONS 1000000
#define WRITE_RATIO 1
#define READ_RATIO 10

// 生成随机英文文本
void
generate_english_text(char *value, size_t size)
{
	const char *samples[] = {
		"Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
		"Cras tristique lobortis est sed blandit.",
		"Donec non lorem finibus ligula molestie sagittis eu ut libero.",
		"Nulla id felis quis eros pellentesque eleifend a sit amet quam.",
		"Donec eu bibendum orci.",
		"Mauris fermentum felis molestie, lacinia erat ac, feugiat est.",
		"In rutrum efficitur dolor vitae varius.",
		"Vivamus dolor nulla, rutrum nec lacus sed, blandit tincidunt urna.",
		"Maecenas ac dolor non tellus dignissim varius.",
		"Cras ultricies tristique odio molestie scelerisque.",
		"Etiam dapibus, risus vel porta volutpat, libero odio accumsan mauris, vitae molestie nibh urna in augue.",
		"Duis gravida orci justo, vel efficitur mi posuere in.",
		"Nunc venenatis viverra lorem sit amet lobortis.",
		"Ut scelerisque non lorem ut viverra.",
		"Etiam quis feugiat sem.",
		"Fusce eu viverra mauris, sit amet tristique nisi.",
		"Curabitur elementum tellus sit amet metus ullamcorper bibendum.",
		"Nulla euismod pellentesque finibus.",
		"Nullam id bibendum libero.",
		"Vivamus nec augue sed nulla accumsan varius.",
		"Morbi dapibus, nibh nec malesuada mattis, diam ante blandit lectus, vitae porta orci arcu et velit.",
		"Aliquam nulla purus, maximus sed dignissim at, consectetur eu neque.",
		"Aliquam pulvinar finibus ultricies.",
		"Curabitur sodales orci molestie diam consectetur vulputate.",
		"Pellentesque malesuada euismod dolor vitae porta.",
		"Phasellus blandit lacinia tempus.",
		"Praesent tellus enim, tempus quis est in, laoreet sagittis ante.",
		"Cras vel magna id ex vestibulum porta.",
		"Donec porttitor imperdiet tellus, ut sollicitudin urna auctor nec.",
		"Nam pharetra at risus tempus venenatis.",
		"Morbi malesuada nisi nulla, vel dapibus odio tincidunt lobortis.",
		"Aenean fringilla maximus consectetur.", "Nulla facilisi.",
		"Nunc non pretium erat.",
		"Ut odio justo, vehicula id cursus in, ornare ac magna.",
		"Praesent ac dignissim quam, sed varius est.",
		"Morbi nec ultricies enim.",
		"Vestibulum iaculis maximus aliquet.",
		"Aenean tincidunt feugiat interdum.",
		"Aliquam interdum vestibulum augue quis consectetur.",
		"Maecenas placerat lacinia arcu ut scelerisque.",
		"Aenean velit ante, volutpat vitae diam et, venenatis rutrum lectus.",
		"Nullam semper tellus ac odio aliquam, nec imperdiet risus tincidunt.",
		"Mauris vitae felis consequat, pulvinar purus at, cursus metus.",
		"Aliquam erat volutpat.",
		"Pellentesque sed eros eget lectus rutrum scelerisque vitae sit amet neque.",
		"Aenean quis scelerisque lacus, nec consectetur quam.",
		"Fusce ullamcorper molestie magna non pellentesque.",
		"Fusce lectus nibh, ultricies ut ante luctus, feugiat venenatis velit.",
		"Sed a aliquet nulla.",
		"Nam volutpat nulla vel velit lacinia euismod.",
		"Maecenas eu sodales lectus.",
		"Nullam id nisi elementum, vulputate odio nec, porta lacus.",
		"Vestibulum dignissim felis eu urna dapibus mollis.",
		"Nullam quis faucibus sapien.",
		"Nunc efficitur leo nunc, at sollicitudin lorem commodo et."
	};
	int num_samples = sizeof(samples) / sizeof(samples[0]);
	int key = rand() % num_samples;
	strncpy(value, samples[key], size);
	value[size - 1] = '\0'; // 确保不会溢出
}

int
main(int argc, char **argv)
{
	srand(time(NULL));
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
	char key[KEY_SIZE], value[VALUE_SIZE];
	long write_times = atoi(argv[1]);
	for (int i = 0; i < write_times; i++) {
		generate_english_text(value, sizeof(value));
		snprintf(key, sizeof(key), "key%d", i);
		reply = redisCommand(c, "SET %s %s", key, value);
		printf("Write key %s: %s\n", key, value);
		freeReplyObject(reply);
	}
	int write_count = 0, read_count = 0;
	printf("write done\n");
	// 先写入数据到指定内存
	for (int i = 0; i < TOTAL_OPERATIONS; i++) {
		if (i % 10 == 0) {
			generate_english_text(value, sizeof(value));
			snprintf(key, sizeof(key), "key%d",
			    rand() % write_times);
			reply = redisCommand(c, "SET %s %s", key, value);
			printf("Write key %s: %s\n", key, value);
			freeReplyObject(reply);
			write_count++;
			if (write_count == READ_RATIO) {
				write_count = 0;
				read_count = 0;
			}
		} else {
			snprintf(key, sizeof(key), "key%d", rand() % i);
			reply = redisCommand(c, "GET %s", rand() % write_times);
			if (reply->type == REDIS_REPLY_STRING) {
				printf("Read key %s: %s\n", key, reply->str);
			}
			freeReplyObject(reply);
			read_count++;
		}
		if (i % 1000 == 0) {
			printf("Operation %d done\n", i);
		}
	}

	redisFree(c);
	return 0;
}
