#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

enum command_exit_type {
    COMMAND_EXIT,
    COMMAND_CONTINUE,
};

struct execute_cmd_result {
    enum command_exit_type type;
    // if type == COMMAND_CONTINUE:
    int status;
};

struct pid_queue {
    pid_t *data;
    size_t size;
    size_t capacity;
};

void pid_queue_push(struct pid_queue *q, pid_t pid) {
    if (q->size == q->capacity) {
        size_t new_capacity = (q->capacity + 1)  * 2;
        q->data = realloc(q->data, new_capacity * sizeof(*q->data));
        q->capacity = new_capacity;
    }
    q->data[q->size++] = pid;
}

pid_t pid_queue_pop(struct pid_queue *q) {
    pid_t temp = q->data[0];
    for (size_t i = 0; i < q->size - 1; ++i) {
        q->data[i] = q->data[i + 1];
    }
    q->size--;
    return temp;
}

void pid_queue_destroy(struct pid_queue *q) {
    free(q->data);
}

void sigchld_handler(int signo) {
    assert(signo == SIGCHLD);
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG))  > 0) {}
}

static struct execute_cmd_result
execute_command_line(struct command_line *line, struct parser *p)
{
    /* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */
    assert(line != NULL);

    if (line->is_background) {
        struct sigaction sa = {
            .sa_handler = sigchld_handler,
            .sa_flags = SA_RESTART
        };
        int err = sigaction(SIGCHLD, &sa, NULL);
        assert(err != -1);

        pid_t child_pid = fork();
        assert(child_pid != -1);

        if (child_pid != 0) {
            struct execute_cmd_result result = {COMMAND_CONTINUE, 0};
            return result;
        }
    }

    int output_fd = STDOUT_FILENO;
    if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
        output_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (output_fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
    } else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
        output_fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (output_fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
    }


    int pipes[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};

    struct pid_queue children_queue = {};

    bool skip_next_command = false;

    const struct expr *prev_e = NULL;

    const struct expr *e = line->head;
    while (e != NULL) {
        if (e->type == EXPR_TYPE_COMMAND) {
            if (skip_next_command) {
                skip_next_command = false;
                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    skip_next_command = true;
                }
                prev_e = e;
                e = e->next;
                continue;
            }

            // TODO: fix for && and ||
            if (strcmp(e->cmd.exe, "cd") == 0 && !e->next) {
                chdir(e->cmd.args[0]);
                break;
            }

            // TODO: fix for && and ||
            if (strcmp(e->cmd.exe, "exit") == 0 && line->head == e && !e->next) {
                struct execute_cmd_result result = {COMMAND_EXIT, EXIT_SUCCESS};
                if (e->cmd.arg_count == 1) {
                    result.status = atoi(e->cmd.args[0]);
                }
                return result;
            }

            bool is_next_pipe = e->next && e->next->type == EXPR_TYPE_PIPE;
            if (is_next_pipe) {
                if (pipe(pipes[2]) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }

            pid_t child_pid = fork();
            if (child_pid == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            pid_queue_push(&children_queue, child_pid);

            if (child_pid == 0) {
                // TODO: fix for && and ||
                if (strcmp(e->cmd.exe, "exit") == 0) {
                    if (e->cmd.arg_count == 1) {
                        pid_queue_destroy(&children_queue);
                        int exit_code = atoi(e->cmd.args[0]);
                        command_line_delete(line);
                        parser_delete(p);
                        exit(exit_code);
                    } else {
                        pid_queue_destroy(&children_queue);
                        command_line_delete(line);
                        parser_delete(p);
                        exit(EXIT_SUCCESS);
                    }
                }

                // Substitute the stdin fd with the reading end of the pipe
                if (prev_e && prev_e->type == EXPR_TYPE_PIPE) {
                    int new_fd = dup2(pipes[1][0], STDIN_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    for (int i = 0; i < 2; ++i) {
                        if (pipes[i][0] != -1) {
                            int res = close(pipes[i][0]);
                            if (res == -1) {
                                perror("close");
                                exit(EXIT_FAILURE);
                            }
                            res = close(pipes[i][1]);
                            if (res == -1) {
                                perror("close");
                                exit(EXIT_FAILURE);
                            }
                            pipes[i][0] = -1;
                            pipes[i][1] = -1;
                        }
                    }
                }

                // TODO: fix for && and ||
                // Substitute the stdout fd with the writing end of the pipe
                if (is_next_pipe) {
                    int new_fd = dup2(pipes[2][1], STDOUT_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    int res = close(pipes[2][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[2][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    pipes[2][0] = -1;
                    pipes[2][1] = -1;
                }

                if (!e->next && (line->out_type == OUTPUT_TYPE_FILE_NEW || line->out_type == OUTPUT_TYPE_FILE_APPEND)) {
                    int new_fd = dup2(output_fd, STDOUT_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                }

                char **argv = malloc((e->cmd.arg_count + 2) * sizeof(*argv));
                argv[0] = e->cmd.exe;
                for (size_t i = 0; i < e->cmd.arg_count; ++i) {
                    argv[i + 1] = e->cmd.args[i];
                }
                argv[e->cmd.arg_count + 1] = NULL;
                int err = execvp(e->cmd.exe, argv);
                if (err == -1) {
                    free(argv);
                    perror("execvp");
                    exit(EXIT_FAILURE);
                }
            }
            if (pipes[0][0] != -1) {
                int res = close(pipes[0][0]);
                if (res == -1) {
                    perror("close");
                    exit(EXIT_FAILURE);
                }
                res = close(pipes[0][1]);
                if (res == -1) {
                    perror("close");
                    exit(EXIT_FAILURE);
                }
            }
            pipes[0][0] = pipes[1][0];
            pipes[0][1] = pipes[1][1];
            pipes[1][0] = pipes[2][0];
            pipes[1][1] = pipes[2][1];
            pipes[2][0] = -1;
            pipes[2][1] = -1;
        } else if (e->type == EXPR_TYPE_PIPE) {
        } else if (e->type == EXPR_TYPE_AND) {
            for (int i = 0; i < 3; ++i) {
                if (pipes[i][0] != -1) {
                    int res = close(pipes[i][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[i][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    pipes[i][0] = -1;
                    pipes[i][1] = -1;
                }
            }
            int wstatus;
            int exit_status = 0;
            while (children_queue.size > 0) {
                pid_t child_pid = pid_queue_pop(&children_queue);
                waitpid(child_pid, &wstatus, 0);
                exit_status = WEXITSTATUS(wstatus);
            }
            if (exit_status != 0) {
                skip_next_command = true;
            }
        } else if (e->type == EXPR_TYPE_OR) {
            for (int i = 0; i < 3; ++i) {
                if (pipes[i][0] != -1) {
                    int res = close(pipes[i][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[i][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    pipes[i][0] = -1;
                    pipes[i][1] = -1;
                }
            }
            int wstatus;
            int exit_status = -1;
            while (children_queue.size > 0) {
                pid_t child_pid = pid_queue_pop(&children_queue);
                waitpid(child_pid, &wstatus, 0);
                exit_status = WEXITSTATUS(wstatus);
            }
            if (exit_status == 0) {
                skip_next_command = true;
            }
        } else {
            assert(false);
        }
        prev_e = e;
        e = e->next;
    }

    if (pipes[0][0] != -1) {
        int res = close(pipes[0][0]);
        if (res == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
        res = close(pipes[0][1]);
        if (res == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    }
    pipes[0][0] = -1;
    pipes[0][1] = -1;

    int exit_status = 0;
    int wstatus;
    while (children_queue.size > 0) {
        pid_t child_pid = pid_queue_pop(&children_queue);
        waitpid(child_pid, &wstatus, 0);
        exit_status = WEXITSTATUS(wstatus);
    }

    pid_queue_destroy(&children_queue);

    if (line->is_background) {
        command_line_delete(line);
        parser_delete(p);
        exit(exit_status);
    }

    struct execute_cmd_result result = {COMMAND_CONTINUE, exit_status};
    return result;
}

struct input_string {
    char *data;
    size_t capacity;
    size_t size;
};

void input_string_append(struct input_string *in_str, const char *str, size_t num_bytes) {
    if (in_str->capacity <= in_str->size + num_bytes) {
        size_t new_capacity = (in_str->capacity + 1) * 2;
        if (new_capacity < in_str->size + num_bytes) {
            new_capacity = in_str->size + num_bytes;
        }
        in_str->data = realloc(in_str->data, new_capacity * sizeof(*in_str->data));
        in_str->capacity = new_capacity;
    } else {
        assert(in_str->capacity > in_str->size + num_bytes);
    }
    memcpy(&in_str->data[in_str->size], str, num_bytes);
    in_str->size += num_bytes;
}

void input_string_reset(struct input_string *in_str) {
    free(in_str->data);
    in_str->data = NULL;
    in_str->size = 0;
    in_str->capacity = 0;
}

int
main(void)
{
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc;
    struct input_string input_string = {};
    struct parser *p = parser_new();
    struct execute_cmd_result result = {COMMAND_CONTINUE, 0};

    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
        input_string_append(&input_string, buf, rc);
        if ((unsigned long)rc == sizeof(buf) && buf[rc - 1] != '\n') {
            continue;
        }

        parser_feed(p, input_string.data, input_string.size);
        input_string_reset(&input_string);

        struct command_line *line = NULL;
        while (true) {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE) {
                printf("Error: %d\n", (int)err);
                continue;
            }
            result = execute_command_line(line, p);
            command_line_delete(line);
        }
        if (result.type == COMMAND_EXIT) {
            break;
        }
    }
    parser_delete(p);
    return result.status;
}
