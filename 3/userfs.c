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
    int index;
};

// This is actually struct inode
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
    /*size_t size;*/
    /*size_t num_blocks;*/
};

/** List of all files. */
static struct file *file_list = NULL;

// This is actually struct file
struct filedesc {
    struct file *file;

    /* PUT HERE OTHER MEMBERS */
    struct block *block;
    int offset;
    int access_mode;
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

static struct file *
create_file(const char *filename)
{
    struct file *last_file = file_list;
    while (last_file && last_file->next) {
        last_file = last_file->next;
    }

    struct file *new_file = calloc(1, sizeof(*new_file));

    size_t len = strlen(filename);
    new_file->name = malloc((len + 1) * sizeof(char));
    strncpy(new_file->name, filename, len + 1);

    new_file->prev = last_file;

    if (last_file) {
        last_file->next = new_file;
    } else {
        file_list = new_file;
    }

    return new_file;
}

int
ufs_open(const char *filename, int flags)
{
    struct file *current_file = file_list;
    while (current_file) {
        if (strcmp(current_file->name, filename) == 0) {
            break;
        }
        current_file = current_file->next;
    }

    // File with the given filename doesn't exist
    if (current_file == NULL) {
        if ((flags & UFS_CREATE) == 0) {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
        if ((flags & UFS_READ_ONLY) != 0) {
            ufs_error_code = UFS_ERR_NO_PERMISSION;
            return -1;
        }
        current_file = create_file(filename);
    }

    struct filedesc *new_file_descriptor = calloc(1, sizeof(*new_file_descriptor));
    new_file_descriptor->file = current_file;
    new_file_descriptor->block = current_file->block_list;

    if ((flags & UFS_READ_ONLY) != 0) {
        new_file_descriptor->access_mode = UFS_READ_ONLY;
    } else if ((flags & UFS_WRITE_ONLY) != 0) {
        new_file_descriptor->access_mode = UFS_WRITE_ONLY;
    } else {
        new_file_descriptor->access_mode = UFS_READ_WRITE;
    }

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
            file_descriptor_capacity = (file_descriptor_capacity + 1) * 2;
            file_descriptors = realloc(file_descriptors, file_descriptor_capacity * sizeof(*file_descriptors));
        }
        file_descriptor_count++;
    }

    file_descriptors[fd] = new_file_descriptor;
    return fd;
}

static struct block *
create_block(struct file *file)
{
    struct block *new_block = calloc(1, sizeof(*new_block));
    new_block->memory = malloc(BLOCK_SIZE * sizeof(char));
    new_block->prev = file->last_block;
    new_block->index = file->last_block ? file->last_block->index + 1 : 0;
    if (file->last_block) {
        file->last_block->next = new_block;
    }
    file->last_block = new_block;
    /*file->num_blocks++;*/
    return new_block;
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

    if (filedesc->access_mode == UFS_READ_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (file->block_list == NULL) {
        file->block_list = create_block(file);
        filedesc->block = file->block_list;
    }

    int fd_position = filedesc->block->index * BLOCK_SIZE + filedesc->offset;
    if (fd_position + size > MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    size_t written = 0;
    while (written < size) {
        if (filedesc->offset == BLOCK_SIZE) {
            if (!filedesc->block->next) {
                filedesc->block->next = create_block(file);
            }
            filedesc->block = filedesc->block->next;
            filedesc->offset = 0;
        }
        filedesc->block->memory[filedesc->offset++] = buf[written++];
        if (filedesc->offset > filedesc->block->occupied) {
            filedesc->block->occupied++;
            /*if (filedesc->block == file->last_block) {*/
                /*file->size++;*/
            /*}*/
        }
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

    if (filedesc->access_mode == UFS_WRITE_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (!filedesc->block) {
        // Maybe someone wrote into the file after this
        // descriptor was opened with an empty file
        filedesc->block = filedesc->file->block_list;
    }

    // New file
    if (!filedesc->block) {
        // EOF
        return 0;
    }

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

static void
deallocate_file(struct file *file)
{
    struct block *current_block = file->block_list;
    while (current_block) {
        struct block *next = current_block->next;
        free(current_block->memory);
        free(current_block);
        current_block = next;
    }
    free(file->name);
    free(file);
}

int
ufs_close(int fd)
{
    if (fd < 0 || fd >= file_descriptor_count || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct file *file = file_descriptors[fd]->file;
    if (--file->refs == 0 && file->is_deleted) {
        deallocate_file(file);
    }

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;

    return 0;
}

int
ufs_delete(const char *filename)
{
    struct file *current_file = file_list;
    while (current_file) {
        if (strcmp(current_file->name, filename) == 0) {
            break;
        }
        current_file = current_file->next;
    }

    if (current_file == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // Delete the file from the file_list
    if (current_file->next) {
        current_file->next->prev = current_file->prev;
    }
    if (current_file->prev) {
        current_file->prev->next = current_file->next;
    } else {
        file_list = current_file->next;
    }

    // Set is_deleted flag if there is an open file descriptor
    // on the file
    if (current_file->refs == 0) {
        deallocate_file(current_file);
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
        struct file *next = current_file->next;
        deallocate_file(current_file);
        current_file = next;
    }
}
