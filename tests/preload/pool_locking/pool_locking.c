#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/stat.h>

#define buffer_size 1024 * 1024

int fd;
char buffer[buffer_size];

char read_buffer[buffer_size];

static void *
f(void *x)
{
	for (int i = 0; i < 1000; i++) {
		ssize_t r = write(fd, buffer, buffer_size);
		(void) r;
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	memset(buffer, '1', buffer_size);

	fd = open(argv[1], O_RDWR | O_CREAT, 0777);

	pthread_t t;
	if (pthread_create(&t, NULL, f, NULL))
		return 1;

	for (int i = 0; i < 100000; i++) {
		close(fd);

		fd = open(argv[1], O_RDWR);
	}

	if (pthread_join(t, NULL))
		return 1;


	close(fd);

	return 0;
}
