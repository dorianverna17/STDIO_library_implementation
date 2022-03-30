#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "so_stdio.h"

#define BUF_SIZE 4096

struct _so_file
{
	char buffer[BUF_SIZE];

	int fd;
	int mode;
	int buffer_position;
	int cursor;

	char *pathname;
};

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *file = malloc(sizeof(SO_FILE));

	int mode_flags;

	if (strcmp(mode, "r") == 0) {
		mode_flags = O_RDONLY;
	} else if (strcmp(mode, "r+") == 0) {
		mode_flags = O_RDWR;
	} else if (strcmp(mode, "w") == 0) {
		mode_flags = O_WRONLY | O_CREAT | O_TRUNC;
	} else if (strcmp(mode, "w+") == 0) {
		mode_flags = O_RDWR | O_CREAT | O_TRUNC;
	} else if (strcmp(mode, "a") == 0) {
		mode_flags = O_APPEND | O_CREAT;
	} else if (strcmp(mode, "a+") == 0) {
		mode_flags = O_APPEND | O_RDONLY | O_CREAT;
	} else {
		free(file);
		return NULL;
	}

	int file_descriptor = open(pathname, mode_flags, 0644);

	if (file_descriptor == -1) {
		free(file);
		return NULL;
	}

	file->fd = file_descriptor;
	file->mode = mode_flags;
	file->pathname = malloc((strlen(pathname) + 1) * sizeof(char));
	strcpy(file->pathname, pathname);
	file->pathname[strlen(pathname)] = '\0';
	file->buffer_position = 0;
	file->cursor = 0;

	return file;
}

int so_fclose(SO_FILE *stream)
{
	int res = close(stream->fd);

	if (res == -1)
		return SO_EOF;

	free(stream->pathname);
	free(stream);

	return 0;
}

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	return 0;
}

long so_ftell(SO_FILE *stream)
{
	return 0;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytes_to_read;
	int cursor_ptr = 0;
	int bytes_to_be_copied;
	int count;

	// total bytes that need to be returned
	bytes_to_read = size * nmemb;

	if (stream->cursor == 0) {
		count = read(stream->fd, stream->buffer, BUF_SIZE);

		if (count == -1)
			return SO_EOF;

		if (count == 0)
			return 0;

		stream->buffer_position = 0;
		stream->cursor += BUF_SIZE;
	}

	// if the buffer does not contain the needed number of bytes
	// then we need to read from file some more
	while ((BUF_SIZE - stream->buffer_position < bytes_to_read)) {
		// copy the available bytes to target ptr
		bytes_to_be_copied = BUF_SIZE - stream->buffer_position;
		if (bytes_to_be_copied > bytes_to_read) {
			memcpy(ptr + cursor_ptr, stream->buffer, bytes_to_read);
			return size * nmemb;
		}
		memcpy(ptr + cursor_ptr, stream->buffer, bytes_to_be_copied);
		cursor_ptr += bytes_to_be_copied;
		bytes_to_read -= bytes_to_be_copied;

		// read bytes from file
		count = read(stream->fd, stream->buffer, BUF_SIZE);

		if (count == -1)
			return SO_EOF;

		if (count < BUF_SIZE) {
			if (bytes_to_read < count)
				memcpy(ptr + cursor_ptr, stream->buffer, bytes_to_read);
			else
				memcpy(ptr + cursor_ptr, stream->buffer, count);
			return size * nmemb;
		}
		// reset the position and update cursor
		stream->buffer_position = 0;
		stream->cursor += BUF_SIZE;
	}
	memcpy(ptr + cursor_ptr, stream->buffer, bytes_to_read);
	stream->buffer_position += bytes_to_read;

	return size * nmemb;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	return 0;
}

int so_fgetc(SO_FILE *stream)
{
	unsigned char res;
	int count;

	if ((stream->buffer_position == BUF_SIZE) || (stream->cursor == 0)) {
		count = read(stream->fd, stream->buffer, BUF_SIZE);
		if (count == 0 || count == -1)
			return SO_EOF;
		stream->buffer_position = 0;
		stream->cursor += BUF_SIZE;
	}
	res = (unsigned char) stream->buffer[stream->buffer_position++];

	return (int) res;
}

int so_fputc(int c, SO_FILE *stream)
{
	return 0;
}

int so_feof(SO_FILE *stream)
{
	return 0;
}

int so_ferror(SO_FILE *stream)
{
	return 0;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

int so_pclose(SO_FILE *stream)
{
	return 0;
}

int main(void)
{
	return 0;
}
