#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 写入数据以使页面变为dirty

#include <time.h>
char lorem[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Quisque imperdiet felis et lobortis viverra. Aenean tristique nulla libero, vel gravida mauris dapibus ut. Integer laoreet sem non enim mollis, ut sollicitudin sapien vulputate. Aenean purus purus, facilisis et venenatis non, condimentum ut tortor. Quisque ac rhoncus enim. Duis laoreet pulvinar ultrices. Aenean vehicula est eu volutpat rutrum. Sed id feugiat quam. Sed nec tempus ligula, sed gravida nisl. Duis tincidunt rhoncus elementum. Aliquam erat volutpat. In odio mi, dapibus in tellus ut, rhoncus vulputate lectus.Ut a viverra purus.Nulla nisl tortor,dignissim quis dui pharetra,euismod rhoncus odio.Nullam eleifend neque non arcu sollicitudinmalesuada.Vivamus eget commodo ex,et aliquam ipsum.Pellentesque diam nibh, posuere non elementum vitae,consectetur feugiatmassa.Aliquam suscipit purus ut metus vestibulum laoreet.Mauris non    lacus lobortis,faucibus nibh non, euismod elit.Aliquam erat volutpat.Vestibulum sagittis,purus eget cursus cursus, sem tortor fringilla mauris,a pretium dolor eros in tellus.Duis sodales commodo sapien,nec malesuada mauris.Nam malesuada vestibulum magna.Aenean suscipit auctorsem,vitae convallis erat fermentum nec.Proin imperdiet pharetra malesuadaVestibulum ante ipsum primis in faucibus orci luctus et ultrices posuerecubilia curae; Curabitur pretium est a eros blandit,sit amet tincidunt ante pulvinar.Donec sed dolor accumsan, aliquam purus id,tempor dolor.Sed fringilla sed risus ac eleifend.Phasellusin venenatis urna.Nullam consequat enim non ante fermentum,ac tristique dolor dictum.Fusce ut tellus malesuada, hendrerit neque tempus,hendrerit massa.Sed diam leo, venenatis a fringilla id,fringilla ut dui.Nunc id faucibus mauris,at pulvinar neque.Duisac maximus neque.Quisque sollicitudin venenatis nunc at luctus.Curabitur    nisl neque,pulvinar quis quam ac, rutrum tempor lectus.Fusce ex enim,pulvinar at dapibus eget, euismod eget sapien.Morbi euismod diam ante,non viverra ipsum vehicula ac.Vestibulum purus ante,sollicitudin id porttitor non,hendrerit quis urna Vestibulum ut ornare lectus";
void
dirty_memory(char *memory, size_t size, int cycle)
{
	struct timespec start, end;
	long seconds, nseconds;
	double time_taken;

	clock_gettime(CLOCK_MONOTONIC, &start);
	int cnt = 0;
	for (size_t i = 0; i < size; i += sysconf(_SC_PAGESIZE)) {
		// memory[i] = (char)(cnt + cycle % 256);
		memcpy(memory + i, lorem, sizeof(lorem);
		cnt++;
		if (cnt % 100000 == 0) {
			printf("now at %d page\n", cnt);
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &end);

	seconds = end.tv_sec - start.tv_sec;
	nseconds = end.tv_nsec - start.tv_nsec;
	time_taken = seconds + nseconds * 1e-9;
	printf("Time taken for dirty: %.3f us\n", time_taken * 1e6);
}

// 读取并检查内存的正确性

int
verify_memory(char *memory, size_t size, int cycle)
{
	struct timespec start, end;
	long seconds, nseconds;
	double time_taken;
	int cnt = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (size_t i = 0; i < size; i += sysconf(_SC_PAGESIZE)) {
		if (memory[i] != (char)(cnt + cycle % 256)) {
			printf(
			    "Memory verification failed at page offset %zu, except : %u, actual : %u\n",
			    i, (char)(cnt + cycle % 256), memory[i]);
			// return 0; // 发现数据错误，返回0
		}
		cnt++;
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	seconds = end.tv_sec - start.tv_sec;
	nseconds = end.tv_nsec - start.tv_nsec;
	time_taken = seconds + nseconds * 1e-9;
	printf("Time taken for verify: %.3f us\n", time_taken * 1e6);
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

		memset(memory, -1, size); // 清零分配的内存

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
