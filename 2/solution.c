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

static struct execute_cmd_result
execute_command_line(struct command_line *line, struct parser *p)
{
    /* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

    assert(line != NULL);
    int output_fd = STDOUT_FILENO;
    if (line->out_type == OUTPUT_TYPE_STDOUT) {
    } else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
        if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
            output_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (output_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        }
    } else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
        if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
            output_fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (output_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        }
    } else {
        assert(false);
    }


    int (*pipes)[2] = NULL;
    int num_pipes = 0;

    pid_t *child_pids = NULL;
    int num_children = 0;

    const struct expr *prev_e = NULL;

    const struct expr *e = line->head;
    while (e != NULL) {
        if (e->type == EXPR_TYPE_COMMAND) {
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

            child_pids = realloc(child_pids, ++num_children * sizeof(*child_pids));

            bool is_next_pipe = e->next && e->next->type == EXPR_TYPE_PIPE;
            int reading_pipe_index = is_next_pipe ? num_pipes - 2 : num_pipes - 1;

            if (is_next_pipe) {
                if (num_pipes > 2) {
                    int res = close(pipes[num_pipes - 3][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[num_pipes - 3][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                }
                pipes = realloc(pipes, ++num_pipes * sizeof(*pipes));
                if (pipe(pipes[num_pipes - 1]) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            } else if (num_pipes > 2) {
                assert(!is_next_pipe);
                int res = close(pipes[num_pipes - 2][0]);
                if (res == -1) {
                    perror("close");
                    exit(EXIT_FAILURE);
                }
                res = close(pipes[num_pipes - 2][1]);
                if (res == -1) {
                    perror("close");
                    exit(EXIT_FAILURE);
                }
            }

            child_pids[num_children - 1] = fork();
            if (child_pids[num_children - 1] == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            if (child_pids[num_children - 1] == 0) {
                // TODO: fix for && and ||
                if (strcmp(e->cmd.exe, "exit") == 0) {
                    if (e->cmd.arg_count == 1) {
                        free(child_pids);
                        free(pipes);
                        int exit_code = atoi(e->cmd.args[0]);
                        command_line_delete(line);
                        parser_delete(p);
                        exit(exit_code);
                    } else {
                        free(child_pids);
                        free(pipes);
                        command_line_delete(line);
                        parser_delete(p);
                        exit(EXIT_SUCCESS);
                    }
                }

                // Close the pipes
                /*bool is_next_pipe = e->next && e->next->type == EXPR_TYPE_PIPE;*/
                /*int reading_pipe_index = is_next_pipe ? num_pipes - 2 : num_pipes - 1;*/
                /*for (int j = num_pipes - 2; j < num_pipes; ++j) {*/
                    /*if (j != reading_pipe_index && j != num_pipes - 1) {*/
                        /*int res = close(pipes[j][0]);*/
                        /*if (res == -1) {*/
                            /*perror("close 3");*/
                            /*exit(EXIT_FAILURE);*/
                        /*}*/
                        /*res = close(pipes[j][1]);*/
                        /*if (res == -1) {*/
                            /*perror("close 4");*/
                            /*exit(EXIT_FAILURE);*/
                        /*}*/
                    /*}*/
                /*}*/

                // Substitute the stdin fd with the reading end of the pipe
                if (prev_e && prev_e->type == EXPR_TYPE_PIPE) {
                    int new_fd = dup2(pipes[reading_pipe_index][0], STDIN_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    int res = close(pipes[reading_pipe_index][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[reading_pipe_index][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                }

                // TODO: fix for && and ||
                // Substitute the stdout fd with the writing end of the pipe
                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    int new_fd = dup2(pipes[num_pipes - 1][1], STDOUT_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    int res = close(pipes[num_pipes - 1][0]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    res = close(pipes[num_pipes - 1][1]);
                    if (res == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
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
            /*if (created_pipe && num_pipes > 2) {*/
                /*int res = close(pipes[num_pipes - 3][0]);*/
                /*if (res == -1) {*/
                    /*perror("close");*/
                    /*exit(EXIT_FAILURE);*/
                /*}*/
                /*res = close(pipes[num_pipes - 3][1]);*/
                /*if (res == -1) {*/
                    /*perror("close");*/
                    /*exit(EXIT_FAILURE);*/
                /*}*/
            /*}*/
        } else if (e->type == EXPR_TYPE_PIPE) {
        } else if (e->type == EXPR_TYPE_AND) {
            int wstatus;
            int exit_status = -1;
            for (int i = 0; i < num_children; ++i) {
                waitpid(child_pids[i], &wstatus, 0);
                exit_status = WEXITSTATUS(wstatus);
            }
            if (exit_status != 0) {
                free(child_pids);
                for (int i = 0; i < num_pipes; ++i) {
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
                }
                free(pipes);
                struct execute_cmd_result result = {COMMAND_CONTINUE, exit_status};
                return result;
            }
        } else if (e->type == EXPR_TYPE_OR) {
            int wstatus;
            int exit_status = -1;
            for (int i = 0; i < num_children; ++i) {
                waitpid(child_pids[i], &wstatus, 0);
                exit_status = WEXITSTATUS(wstatus);
            }
            if (exit_status == 0) {
                free(child_pids);
                for (int i = 0; i < num_pipes; ++i) {
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
                }
                free(pipes);
                struct execute_cmd_result result = {COMMAND_CONTINUE, exit_status};
                return result;
            }
        } else {
            assert(false);
        }
        prev_e = e;
        e = e->next;
    }

    if (num_pipes > 2) {
        for (int i = num_pipes - 2; i < num_pipes; ++i) {
            int res = close(pipes[i][0]);
            if (res == -1) {
                perror("close 1");
                exit(EXIT_FAILURE);
            }
            res = close(pipes[i][1]);
            if (res == -1) {
                perror("close 2");
                exit(EXIT_FAILURE);
            }
        }
    }
    free(pipes);

    int exit_status = 0;
    if (!line->is_background) {
        int wstatus;
        for (int i = 0; i < num_children; ++i) {
            waitpid(child_pids[i], &wstatus, 0);
            exit_status = WEXITSTATUS(wstatus);
        }
    }
    free(child_pids);

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
