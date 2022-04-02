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
	int curr_buff_size;

	// 0 for read, 1 for write;
	int read_write;

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
		mode_flags = O_APPEND | O_WRONLY | O_CREAT;
	} else if (strcmp(mode, "a+") == 0) {
		mode_flags = O_APPEND | O_RDWR | O_CREAT;
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
	file->curr_buff_size = 0;
	file->read_write = -1;

	return file;
}

int so_fclose(SO_FILE *stream)
{
	int res;

	so_fflush(stream);

	res = close(stream->fd);

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
	int res;
	int stream_size = stream->buffer_position;
	int current_cursor = 0;

	while (stream_size > 0) {
		res = write(stream->fd, stream->buffer + current_cursor, stream_size);

		if (res == -1)
			return SO_EOF;

		current_cursor += res;
		stream_size -= res;
	}

	stream->buffer_position = 0;
	stream->curr_buff_size = 0;

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int res;

	if (whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END)
		res = lseek(stream->fd, offset, whence);
	else
		return -1;

	if (res == -1)
		return -1;
	stream->cursor = res;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	return stream->cursor;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytes_to_read;
	int cursor_ptr = 0;
	int bytes_to_be_copied;
	int count;
	int bytes_unread_buffer;

	if (stream->read_write == 1) {
		stream->buffer_position = 0;
		stream->curr_buff_size = 0;
	}
	stream->read_write = 0;

	// total bytes that need to be returned
	bytes_to_read = size * nmemb;

	while (bytes_to_read > 0) {
		bytes_unread_buffer = stream->curr_buff_size - stream->buffer_position;

		if (bytes_unread_buffer > bytes_to_read) {
			memcpy(ptr + cursor_ptr, stream->buffer + stream->buffer_position, bytes_to_read);
			stream->buffer_position += bytes_to_read;
			bytes_to_read = 0;
			cursor_ptr += bytes_to_read;
		} else {
			memcpy(ptr + cursor_ptr, stream->buffer + stream->buffer_position, bytes_unread_buffer);
			stream->buffer_position += bytes_unread_buffer;
			bytes_to_read -= bytes_unread_buffer;
			cursor_ptr += bytes_unread_buffer;
		}

		if ((bytes_to_read > 0) && (stream->curr_buff_size == stream->buffer_position)) {
			count = read(stream->fd, stream->buffer, BUF_SIZE);
			if (count < 0)
				return SO_EOF;

			if (count == 0)
				return nmemb;

			stream->buffer_position = 0;
			if (bytes_to_read > count)
				stream->cursor += count;
			else
				stream->cursor += bytes_to_read;
			stream->curr_buff_size = count;
		}
	}
	return nmemb;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytes_free;
	int bytes_to_write = size * nmemb;
	int ptr_cursor = 0;

	if (stream->read_write == 0) {
		stream->buffer_position = 0;
		stream->curr_buff_size = 0;
	}
	stream->read_write = 1;

	while (bytes_to_write > 0) {
		if (BUF_SIZE - stream->buffer_position <= 0)
			return SO_EOF;

		bytes_free = BUF_SIZE - stream->buffer_position;

		if (bytes_free > bytes_to_write) {
			memcpy(stream->buffer + stream->buffer_position, ptr + ptr_cursor, bytes_to_write);
			stream->buffer_position += bytes_to_write;
			ptr_cursor += bytes_to_write;
			bytes_to_write = 0;
		} else {
			memcpy(stream->buffer + stream->buffer_position, ptr + ptr_cursor, bytes_free);
			stream->buffer_position += bytes_free;
			ptr_cursor += bytes_free;
			bytes_to_write -= bytes_free;
			so_fflush(stream);
		}
	}
	return nmemb;
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
	int res;

	memcpy(stream->buffer + stream->buffer_position, (char *) &c, 1);

	stream->buffer_position++;

	if (stream->buffer_position == BUF_SIZE)
		so_fflush(stream);

	return c;
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
