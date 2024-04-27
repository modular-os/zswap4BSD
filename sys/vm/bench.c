#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 写入数据以使页面变为dirty

void
dirty_memory(char *memory, size_t size, int cycle)
{
	clock_t start, end;
	double cpu_time_used;
	start = clock();

	for (size_t i = 0; i < size; i += sysconf(_SC_PAGESIZE)) {
		memory[i] = (char)(i + cycle % 256); // 使用页的偏移作为数据值
	}

	end = clock();
	cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
	printf("Time taken for dirty: %f us\n", cpu_time_used * 1e6);
}

// 读取并检查内存的正确性

int
verify_memory(char *memory, size_t size, int cycle)
{
	for (size_t i = 0; i < size; i += sysconf(_SC_PAGESIZE)) {
		if (memory[i] != (char)(i + cycle % 256)) {
			printf(
			    "Memory verification failed at page offset %zu\n",
			    i);
			return 0; // 发现数据错误，返回0
		}
	}
	printf("Memory verification succeeded.\n");
	return 1; // 数据正确，返回1
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr,
		    "Usage: %s <memory size in MB> [number of cycles]\n",
		    argv[0]);
		return 1;
	}

	long mb = atol(argv[1]);
	if (mb <= 0) {
		fprintf(stderr, "Memory size must be a positive number\n");
		return 1;
	}

	int cycles = 3; // 默认执行3轮
	if (argc > 2) {
		cycles = atoi(argv[2]);
		if (cycles <= 0) {
			fprintf(stderr,
			    "Number of cycles must be a positive number\n");
			return 1;
		}
	}

	printf("Starting memory dirty and verify test...\n");

	for (int i = 0; i < cycles; i++) {

		size_t size = mb * 1024 * 1024; // MB转换为字节数
		char *memory = malloc(size);
		if (memory == NULL) {
			perror("Failed to allocate memory");
			return 1;
		}

		memset(memory, 0, size); // 清零分配的内存

		printf("Cycle %d:\n", i + 1);
		dirty_memory(memory, size, i);
		if (!verify_memory(memory, size, i)) {
			printf("Memory verification failed in cycle %d\n",
			    i + 1);
			free(memory);
			return 1;
		}

		free(memory);
	}

	printf("Test completed successfully.\n");
	return 0;
}
