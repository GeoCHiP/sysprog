#include "userfs.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    int occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;

    /* PUT HERE OTHER MEMBERS */
};

struct file {
    /** Double-linked list of file blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    /* PUT HERE OTHER MEMBERS */
    bool is_deleted;
    size_t size;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
    struct file *file;

    /* PUT HERE OTHER MEMBERS */
    struct block *block;
    int offset;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
    return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
    struct file *current_file = file_list;
    struct file *last_file = NULL;
    while (current_file != NULL) {
        if (strcmp(current_file->name, filename) == 0 && current_file->is_deleted == false) {
            break;
        }
        if (current_file->next == NULL)  {
            last_file = current_file;
        }
        current_file = current_file->next;
    }

    // File with the given filename doesn't exist
    if (current_file == NULL) {
        if ((flags & UFS_CREATE) == 0) {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }

        // TODO: extract file creation into a separate function
        struct file *new_file = malloc(sizeof(struct file));

        size_t len = strlen(filename);
        new_file->name = malloc((len + 1) * sizeof(char));
        strncpy(new_file->name, filename, len + 1);

        new_file->block_list = NULL;
        new_file->last_block = NULL;
        new_file->refs = 0;
        new_file->next = NULL;
        new_file->prev = last_file;
        new_file->is_deleted = false;

        if (last_file != NULL) {
            last_file->next = new_file;
        }
        if (file_list == NULL) {
            file_list = new_file;
        } else {
            last_file->next = new_file;
        }
        current_file = new_file;
    }

    struct filedesc *new_file_descriptor = calloc(1, sizeof(struct filedesc));
    new_file_descriptor->file = current_file;
    new_file_descriptor->block = new_file_descriptor->file->block_list;
    current_file->refs++;

    // Search for available file descriptors
    int fd = file_descriptor_count;
    for (int i = 0; i < file_descriptor_count; ++i) {
        if (file_descriptors[i] == NULL) {
            fd = i;
            break;
        }
    }

    // No available file descriptors, so allocate more
    if (fd == file_descriptor_count) {
        if (file_descriptor_count >= file_descriptor_capacity) {
            int new_capacity = (file_descriptor_capacity + 1) * 2;
            file_descriptors = realloc(file_descriptors, new_capacity * sizeof(struct filedesc *));
        }
        file_descriptor_count++;
    }

    file_descriptors[fd] = new_file_descriptor;
    return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    struct file *file = filedesc->file;

    if (size > MAX_FILE_SIZE || file->size + size > MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    // TODO: check for MAX_FILE_SIZE
    if (file->block_list == NULL) {
        /*size_t num_blocks = 0;*/
        /*if (size % BLOCK_SIZE != 0) {*/
            /*// Round up the number of blocks*/
            /*num_blocks = (size + (BLOCK_SIZE - size % BLOCK_SIZE)) / BLOCK_SIZE;*/
        /*} else {*/
            /*num_blocks = size / BLOCK_SIZE;*/
        /*}*/
        file->block_list = calloc(1, sizeof(struct block));
    }

    struct block *current_block = file->block_list;
    size_t written = 0;

    while (written < size) {
        if (current_block->occupied == BLOCK_SIZE) {
            struct block *new_block = calloc(1, sizeof(struct block));
            new_block->prev = current_block;
            current_block->next = new_block;
            current_block = new_block;
            filedesc->block = new_block;
            filedesc->offset = 0;
        }
        if (current_block->memory == NULL) {
            current_block->memory = malloc(BLOCK_SIZE * sizeof(char));
        }
        current_block->memory[filedesc->offset] = buf[written];
        filedesc->offset++;
        current_block->occupied++;
        written++;
        file->size++;
    }

    return written;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    struct block *current_block = filedesc->block;
    size_t read = 0;
    while (read < size) {
        if (filedesc->offset == BLOCK_SIZE) {
            current_block = current_block->next;
            filedesc->block = current_block;
            filedesc->offset = 0;
        } else if (filedesc->offset == filedesc->block->occupied) {
            break;
        }
        if (current_block == NULL) {
            break;
        }
        buf[read++] = current_block->memory[filedesc->offset++];
    }

    return read;
}

int
ufs_close(int fd)
{
    if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // TODO: if the last fd and the file is deleted free() the file
    struct file *file = file_descriptors[fd]->file;
    if (--file->refs == 0 && file->is_deleted) {
        free(file);
    }

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;

    return 0;
}

int
ufs_delete(const char *filename)
{
    struct file *current_file = file_list;
    while (current_file != NULL) {
        if (strcmp(current_file->name, filename) == 0) {
            break;
        }
        current_file = current_file->next;
    }

    if (current_file == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (current_file->next != NULL) {
        current_file->next->prev = current_file->prev;
    }
    if (current_file->prev != NULL) {
        current_file->prev->next = current_file->next;
    }
    if (current_file->next == NULL && current_file->prev == NULL) {
        assert(current_file == file_list);
        file_list = NULL;
    }

    // TODO: free() struct file if there are no open fd
    if (current_file->refs == 0) {
        free(current_file);
    } else {
        current_file->is_deleted = true;
    }
    return 0;
}

void
ufs_destroy(void)
{
    for (int i = 0; i < file_descriptor_count; ++i) {
        if (file_descriptors[i] != NULL) {
            ufs_close(i);
        }
    }
    free(file_descriptors);
    struct file *current_file = file_list;
    while (current_file != NULL) {
        // TODO: add freeing all the blocks
        free(current_file->name);
        free(current_file);
    }
}
