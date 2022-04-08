#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#include "so_stdio.h"

#define BUF_SIZE 4096

struct _so_file {
	/* buffer used for read write operations */
	char buffer[BUF_SIZE];
	/* the file descriptor */
	int fd;
	/* the mode used for the file */
	int mode;
	/* the position of the cursor in the buffer */
	int buffer_position;
	/* the position of the cursor in the file */
	int cursor;
	/* 
	 * current size of the buffer stored
	 * in the buffer field
	 */
	int curr_buff_size;
	/* 0 for read, 1 for write;*/
	int read_write;
	/* used by ferror, 0 if not error, 1 if error */
	int error;
	/* pid of child process */
	int pid;
	/*
	 * flag that says if the child
	 * process got tp the end of the file
	 */
	int child_at_end;
	/* path of the file */
	char *pathname;
};

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *file = malloc(sizeof(SO_FILE));
	int mode_flags;

	/* figure out which flags to be used for the open function */
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

	/* open the file */
	int file_descriptor = open(pathname, mode_flags, 0644);

	/* treat the error */
	if (file_descriptor == -1) {
		free(file);
		return NULL;
	}

	/* initialize all the other members of the structure */
	file->fd = file_descriptor;
	file->mode = mode_flags;
	file->pathname = malloc((strlen(pathname) + 1) * sizeof(char));
	strcpy(file->pathname, pathname);
	file->pathname[strlen(pathname)] = '\0';
	file->buffer_position = 0;
	file->cursor = 0;
	file->curr_buff_size = 0;
	file->read_write = -1;
	file->error = 0;
	file->pid = -1;
	file->child_at_end = 0;

	return file;
}

int so_fclose(SO_FILE *stream)
{
	int res;
	int res_ferror = 0;
	int status;

	/*
	 * if the buffer cursor is not at its beginning and if
	 * we are after a write process, then we should flush the
	 * output once again
	 */
	if (stream->buffer_position > 0 && stream->read_write == 1) {
		so_fflush(stream);
		res_ferror = stream->error;
	}

	/* close the file */
	res = close(stream->fd);

	/* free allocated memory */
	free(stream->pathname);
	free(stream);

	if (res_ferror == 1)
		return SO_EOF;

	if (res == -1)
		return SO_EOF;
	return 0;
}

int so_fileno(SO_FILE *stream)
{
	/* return the file descriptor */
	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	int res;
	int stream_size = stream->buffer_position;
	int current_cursor = 0;

	/*
	 *write the data that is currently in the buffer
	 * use a while loop as the write call will not always
	 * return a value equal to stream_size
	 */
	while (stream_size > 0) {
		if (stream->read_write == 0)
			break;

		/* write to file */
		res = write(stream->fd, stream->buffer + current_cursor, stream_size);

		/* treat error */
		if (res == -1) {
			stream->error = 1;
			return SO_EOF;
		}

		/* set the new cursors and the new buffer size */
		current_cursor += res;
		stream_size -= res;
		stream->cursor += res;
	}

	/* reinitialize the buffer */
	stream->buffer_position = 0;
	stream->curr_buff_size = 0;

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int res;

	/*
	 * if the previous read-write operation was a write
	 * then we need to fluch the buffer once again
	 */
	if (stream->read_write == 1) {
		if (stream->buffer_position > 0)
			so_fflush(stream);
		stream->read_write = -1;
	}

	/* call lseek to move the actual cursor in the file */
	if (whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END)
		res = lseek(stream->fd, offset, whence);
	else
		return -1;

	if (res == -1)
		return -1;
	/* update the cursor field and reset the buffer fields */
	stream->cursor = res;
	stream->buffer_position = 0;
	stream->curr_buff_size = 0;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	/* return the current cursor */
	return stream->cursor;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int bytes_to_read;
	int cursor_ptr = 0;
	int bytes_read_res = 0;
	int count;
	int bytes_unread_buffer;

	/*
	 * if read is called after a write, then we have
	 * to reset the buffer
	 */
	if (stream->read_write == 1) {
		stream->buffer_position = 0;
		stream->curr_buff_size = 0;
	}
	stream->read_write = 0;

	/* total bytes that need to be returned */
	bytes_to_read = size * nmemb;

	/* read inside a while loop until we read all the bytes that we needed */
	while (bytes_to_read > 0) {
		bytes_unread_buffer = stream->curr_buff_size - stream->buffer_position;

		/*
		 * if the number of bytes free left in the buffer is greater than the
		 * number of bytes left to read, then we copy the number of bytes
		 * that we have to read in the buffer
		 */
		if (bytes_unread_buffer > bytes_to_read) {
			memcpy(ptr + cursor_ptr, stream->buffer + stream->buffer_position, bytes_to_read);
			stream->buffer_position += bytes_to_read;
			bytes_to_read = 0;
			cursor_ptr += bytes_to_read;
		/*
		 * if the above condition is not satisfied, then we copy the number of
		 * bytes free left in the buffer
		 */
		} else {
			memcpy(ptr + cursor_ptr, stream->buffer + stream->buffer_position, bytes_unread_buffer);
			stream->buffer_position += bytes_unread_buffer;
			bytes_to_read -= bytes_unread_buffer;
			cursor_ptr += bytes_unread_buffer;
		}

		/* read more bytes until we filled the buffer */
		if ((bytes_to_read > 0) && (stream->curr_buff_size == stream->buffer_position)) {
			count = read(stream->fd, stream->buffer, BUF_SIZE);

			/* treat the error */
			if (count < 0) {
				stream->error = 1;
				return 0;
			}

			/* if we do not read anymore, then we should stop */
			if (count == 0) {
				if (bytes_to_read > 0) {
					if (stream->pid != -1)
						stream->child_at_end = 1;
					stream->buffer_position++;
					return nmemb - bytes_to_read / size;
				}
				return nmemb;
			}

			/* reset buffer and set the buffer and file cursors */
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

	/*
	 * if we had a read operation before
	 * then we should reset the buffer
	 */
	if (stream->read_write == 0) {
		stream->buffer_position = 0;
		stream->curr_buff_size = 0;
	}
	stream->read_write = 1;

	/* write to buffer while we have bytes to write */
	while (bytes_to_write > 0) {
		/* treat error */
		if (BUF_SIZE - stream->buffer_position <= 0)
			return SO_EOF;

		/* the number of bytes that can be written */
		bytes_free = BUF_SIZE - stream->buffer_position;

		/* similar to read if block - write the bytes to write */
		if (bytes_free > bytes_to_write) {
			memcpy(stream->buffer + stream->buffer_position, ptr + ptr_cursor, bytes_to_write);
			stream->buffer_position += bytes_to_write;
			ptr_cursor += bytes_to_write;
			stream->cursor += bytes_to_write;
			bytes_to_write = 0;
		/* write the bytes that we can write and flush the buffer */
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

	stream->read_write = 0;

	/* if the buffer is empty or if it is filled, then we should read more */
	if ((stream->buffer_position == stream->curr_buff_size) || (stream->cursor == 0)) {
		count = read(stream->fd, stream->buffer, BUF_SIZE);
		if (count == 0 || count == -1) {
			stream->buffer_position++;
			return SO_EOF;
		}

		stream->buffer_position = 0;
		stream->cursor += count;
		stream->curr_buff_size = count;
	}
	/* return the last byte */
	res = (unsigned char) stream->buffer[stream->buffer_position++];

	return (int) res;
}

int so_fputc(int c, SO_FILE *stream)
{
	int res;

	stream->read_write = 1;

	/* copy one byte to the buffer */
	memcpy(stream->buffer + stream->buffer_position, (char *) &c, 1);

	stream->buffer_position++;

	/*
	 * if we reached the end of the buffer
	 * then we should flush the buffer
	 */
	if (stream->buffer_position == BUF_SIZE)
		so_fflush(stream);

	return c;
}

int so_feof(SO_FILE *stream)
{
	int current_cursor;
	int current_position;
	int res;

	/* return if the child process got to the end */
	if (stream->pid != -1 && stream->child_at_end == 1)
		return 1;

	/* compare the current cursor with the number of bytes to the end */
	current_cursor = so_ftell(stream);
	current_position = stream->buffer_position + current_cursor - stream->curr_buff_size;
	res = lseek(stream->fd, 0, SEEK_END);

	if (res == -1)
		return 0;

	if (res < current_position)
		return 1;

	/* put the actual cursor where it was */
	res = lseek(stream->fd, current_cursor, SEEK_SET);

	return 0;
}

int so_ferror(SO_FILE *stream)
{
	/* return error status */
	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	int pid, arguments_size = 3;
	SO_FILE *stream = malloc(sizeof(SO_FILE));
	char **arguments, *p;
	char separators[2] = " ";
	int res, file_desc[2], status;
	int res_exec;

	/* initialize buffer fields */
	stream->buffer_position = 0;
	stream->cursor = 0;
	stream->curr_buff_size = 0;
	stream->read_write = -1;
	stream->error = 0;
	stream->child_at_end = 0;

	/* create pipe */
	res = pipe(file_desc);

	if (res == -1)
		return NULL;

	/* set file desciptor based on the type given */
	if (strcmp(type, "r") == 0)
		stream->fd = file_desc[0];
	else if (strcmp(type, "w") == 0)
		stream->fd = file_desc[1];
	else
		return NULL;

	/* initialize arguments */
	arguments = malloc(4 * sizeof(char *));
	arguments[0] = malloc(3 * sizeof(char));
	strcpy(arguments[0], "sh");
	arguments[0][2] = '\0';
	arguments[1] = malloc(3 * sizeof(char));
	strcpy(arguments[1], "-c");
	arguments[1][2] = '\0';
	arguments[2] = malloc((strlen(command) + 1) * sizeof(char));
	strcpy(arguments[2], command);
	arguments[2][strlen(command)] = '\0';
	arguments[3] = NULL;

	/* create child process */
	pid = fork();

	/* if we had an error then we should treat it */
	if (pid < 0) {
		close(file_desc[0]);
		close(file_desc[1]);
		for (int i = 0; i < arguments_size; i++) {
			if (arguments[i])
				free(arguments[i]);
		}
		free(arguments);
		free(stream);
		return NULL;
	}

	/* if pid is 0 then we are in the child */
	if (pid == 0) {
		/*
		 * based on the type, close the not necessary channels
		 * and create a link to either stdin or stdout
		 */
		if (strcmp(type, "r") == 0) {
			close(file_desc[0]);
			dup2(file_desc[1], STDOUT_FILENO);
		} else if (strcmp(type, "w") == 0) {
			close(file_desc[1]);
			dup2(file_desc[0], STDIN_FILENO);
		}

		/* execute the command */
		res_exec = execv("/bin/sh", arguments);
		if (res_exec)
			return NULL;
	} else {
		/* in the parent set the pid */
		stream->pid = pid;
		/* close the not necessary channels */
		if (strcmp(type, "r") == 0)
			close(file_desc[1]);
		else if (strcmp(type, "w") == 0)
			close(file_desc[0]);
	}

	/* free allocated memory */
	for (int i = 0; i < arguments_size; i++) {
		if (arguments[i])
			free(arguments[i]);
	}
	free(arguments);

	return stream;
}

int so_pclose(SO_FILE *stream)
{
	int res = -1, status;

	/*
	 * if there was a write operation, then we should flush
	 * the buffer, free the memory and close the file
	 */
	if (stream->buffer_position > 0 && stream->read_write == 1) {
		so_fflush(stream);
		close(stream->fd);
		free(stream);
		return 0;
	}

	/* wait for the process */
	if (stream->pid != -1)
		res = waitpid(stream->pid, &status, 0);

	/* close the file and free the memory */
	close(stream->fd);
	free(stream);

	if (res >= 0)
		return 0;

	return -1;
}
