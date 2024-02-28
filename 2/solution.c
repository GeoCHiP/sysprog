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
execute_command_line(const struct command_line *line)
{
    /* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

    assert(line != NULL);
    printf("================================\n");
    printf("Command line:\n");
    printf("Is background: %d\n", (int)line->is_background);
    printf("Output: ");

    int output_fd = STDOUT_FILENO;
    if (line->out_type == OUTPUT_TYPE_STDOUT) {
        printf("stdout\n");
    } else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
        printf("new file - \"%s\"\n", line->out_file);
        if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
            output_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (output_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        }
    } else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
        printf("append file - \"%s\"\n", line->out_file);
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
    size_t num_pipes = 0;
    size_t command_index = 0;

    pid_t *child_pids = NULL;
    size_t num_chidren = 0;

    const struct expr *prev_e = NULL;

    printf("Expressions:\n");
    const struct expr *e = line->head;
    while (e != NULL) {
        if (e->type == EXPR_TYPE_COMMAND) {
            printf("\tCommand: %s", e->cmd.exe);
            for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
                printf(" %s", e->cmd.args[i]);
            printf("\n");

            // TODO: fix for && and ||
            if (strcmp(e->cmd.exe, "cd") == 0 && !e->next) {
                chdir(e->cmd.args[0]);
                break;
            }

            // TODO: fix for && and ||
            if (strcmp(e->cmd.exe, "exit") == 0 && !e->next) {
                struct execute_cmd_result result = {COMMAND_EXIT, -1};
                return result;
            }

            ++command_index;

            child_pids = realloc(child_pids, ++num_chidren * sizeof(*child_pids));

            if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                pipes = realloc(pipes, ++num_pipes * sizeof(*pipes));
                if (pipe(pipes[num_pipes - 1]) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }


            child_pids[num_chidren - 1] = fork();
            if (child_pids[num_chidren - 1] == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            if (child_pids[num_chidren - 1] == 0) {
                // Close the pipes
                bool is_next_pipe = e->next && e->next->type == EXPR_TYPE_PIPE;
                size_t reading_pipe_index = is_next_pipe ? num_pipes - 2 : num_pipes - 1;
                for (size_t j = 0; j < num_pipes; ++j) {
                    if (j != reading_pipe_index && j != num_pipes - 1) {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }
                }

                // Substitute the stdin fd with the reading end of the pipe
                if (prev_e && prev_e->type == EXPR_TYPE_PIPE) {
                    int new_fd = dup2(pipes[reading_pipe_index][0], STDIN_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(pipes[reading_pipe_index][1]);
                }

                // TODO: fix for && and ||
                // Substitute the stdout fd with the writing end of the pipe
                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    int new_fd = dup2(pipes[num_pipes - 1][1], STDOUT_FILENO);
                    if (new_fd == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(pipes[num_pipes - 1][0]);
                }

                if (!e->next && (line->out_type == OUTPUT_TYPE_FILE_NEW || line->out_type == OUTPUT_TYPE_FILE_APPEND)) {
                    int new_fd = dup2(output_fd, STDOUT_FILENO);
                    if (new_fd == -1) {
                        perror("dup2 out file");
                        exit(EXIT_FAILURE);
                    }
                }

                char **argv = malloc((e->cmd.arg_count + 1) * sizeof(*argv));
                argv[0] = e->cmd.exe;
                for (size_t i = 0; i < e->cmd.arg_count; ++i) {
                    argv[i + 1] = e->cmd.args[i];
                }
                execvp(e->cmd.exe, argv);
            }
        } else if (e->type == EXPR_TYPE_PIPE) {
            printf("\tPIPE\n");
        } else if (e->type == EXPR_TYPE_AND) {
            printf("\tAND\n");
        } else if (e->type == EXPR_TYPE_OR) {
            printf("\tOR\n");
        } else {
            assert(false);
        }
        prev_e = e;
        e = e->next;
    }

    for (size_t i = 0; i < num_pipes; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    int wstatus;
    int exit_status = -1;
    printf("num_children == %zu\n", num_chidren);
    for (size_t i = 0; i < num_chidren; ++i) {
        waitpid(child_pids[i], &wstatus, 0);
        exit_status = WEXITSTATUS(wstatus);
        if (WIFEXITED(wstatus)) {
            printf("parent: child_%zu exited normally with status %d\n", i, exit_status);
        }
        if (WIFSIGNALED(wstatus)) {
            printf("parent: child_%zu was terminated by signal %d\n", i, WTERMSIG(wstatus));
        }
    }
    free(child_pids);

    struct execute_cmd_result result = {COMMAND_CONTINUE, exit_status};
    return result;
}

int
main(void)
{
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc;
    struct parser *p = parser_new();

    write(STDOUT_FILENO, "> ", 2);
    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
        parser_feed(p, buf, rc);
        struct command_line *line = NULL;
        struct execute_cmd_result result;
        while (true) {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE) {
                printf("Error: %d\n", (int)err);
                continue;
            }
            result = execute_command_line(line);
            command_line_delete(line);
        }
        if (result.type == COMMAND_EXIT) {
            break;
        }
        write(STDOUT_FILENO, "> ", 2);
    }
    parser_delete(p);
    return 0;
}
