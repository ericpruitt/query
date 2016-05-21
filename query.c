/**
 * This tool reads a list of files from stdin, pipes the contents into the
 * specified COMMAND and prints the name of the file if the command succeeds.
 * Refer to the "usage" function for more information.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void free_line_buffer(void);
int main(int, char **);
void sigusr1_handler(int) __attribute__((noreturn));
void usage(char *);

/**
 * Pointer to buffer used by getline(3) and getdelim(3).
 */
static char *line = NULL;

/**
 * Ways of handling file name delimation.
 */
typedef enum {
    LINE_DELIMATION,
    NULL_BYTE_DELIMATION,
    ASCII_WHITESPACE_DELIMATION,
} delimation_et;

/**
 * Display application usage information.
 *
 * @param self  Name or path of compiled executable.
 */
void usage(char *self)
{
    printf(
        "Usage: %s [OPTION] [!] COMMAND [ARGUMENT...]\n"
        "\n"
        "This tool reads a list of files from stdin, pipes the contents of "
        "each file\ninto the specified command and prints the name of the "
        "file if the command\nsucceeds. The name of the file is exposed to "
        "the command via the environment\nvariable QUERY_FILENAME.\n"
        "\n"
        "Option parsing stops at the first non-option argument.\n"
        "\n"
        "Exit statuses:\n"
        " 1     Fatal error encountered.\n"
        " 2     Non-fatal error encountered.\n"
        "\n"
        "Options:\n"
        " -!    Only print filenames when the COMMAND fails.\n"
        " -0    File names are delimited by null bytes.\n"
        " -h    Show this text and exit.\n"
        " -n    File names are line-delimited. This the default behavior.\n"
        " -s    Redirect stderr from the COMMAND to /dev/null.\n"
        " -w    File names are delimited by ASCII whitespace.\n"
        , self
    );
}

/**
 * Free memory allocated for getline(3) and getdelim(3).
 */
void free_line_buffer(void)
{
    free(line);
}

/**
 * Handler for SIGUSR1 that makes the program exit with a status of 1. The
 * signal is sent by the child process after a fork to indicate that execvp(3)
 * failed which is a fatal error.
 */
void sigusr1_handler(int signal)
{
    exit(1);
}

int main(int argc, char **argv)
{
    pid_t child_pid;
    char *cursor;
    int dev_null_fd;
    char *eol;
    int errout_fd;
    struct stat file_status;
    const char *getline_function;
    int input_fd;
    ssize_t line_length;
    int option;
    int return_code;
    pid_t status;

    delimation_et delimation = LINE_DELIMATION;
    int display_on_success = 1;
    size_t buffer_length = 0;
    int non_fatal_errors = 0;
    int redirect_stderr = 0;

    while ((option = getopt(argc, argv, "+!0hnsw")) != -1) {
        switch (option) {
          case '!':
            display_on_success = 0;
            break;
          case '0':
            delimation = NULL_BYTE_DELIMATION;
            break;
          case 'h':
            usage(argv[0]);
            return 0;
          case 'n':
            delimation = LINE_DELIMATION;
            break;
          case 's':
            redirect_stderr = 1;
            break;
          case 'w':
            delimation = ASCII_WHITESPACE_DELIMATION;
            break;
          case '+':
            // Using "+" to ensure POSIX-style argument parsing is a GNU
            // extension, so an explicit check for "+" as a flag is added for
            // other getopt(3) implementations.
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], option);
          default:
            return 1;
        }
    }

    // Passing "!" as the first non-option argument is the same as using "-!".
    if (optind < argc && argv[optind][0] == '!' && argv[optind][1] == '\0') {
        display_on_success = 0;
        optind++;
    }

    if (optind >= argc) {
        fputs("No command specified.\n", stderr);
        return 1;
    } else if ((dev_null_fd = open("/dev/null", O_WRONLY)) == -1) {
        perror("/dev/null");
        return 1;
    } else if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    if (delimation == NULL_BYTE_DELIMATION) {
        getline_function = "getdelim";
    } else {
        getline_function = "getline";
    }

    errout_fd = redirect_stderr ? dev_null_fd : STDERR_FILENO;
    atexit(free_line_buffer);

next_line:
    // There is no signal handling or EINTR retry logic because under normal
    // operation, the only command that could possibly be interrupted by an
    // expected syscall is close(2) on a file opened with O_RDONLY.
    while (1) {
        if (delimation == NULL_BYTE_DELIMATION) {
            line_length = getdelim(&line, &buffer_length, (int) '\0', stdin);
        } else {
            line_length = getline(&line, &buffer_length, stdin);
        }

        if (line_length == -1) {
            if (feof(stdin)) {
                break;
            }
            perror(getline_function);
            return 1;
        } else {
            eol = line + line_length;
        }

        // When using line and whitespace delimation, insert null bytes so that
        // a pointer to the beginning of the field can be used to represent the
        // path of the file being opened.
        if (delimation == LINE_DELIMATION) {
            if (line[line_length - 1] == '\n') {
                line[line_length - 1] = '\0';
            }
        } else if (delimation == ASCII_WHITESPACE_DELIMATION) {
            for (cursor = line; cursor < eol; cursor++) {
                if (isspace(*cursor)) {
                    *cursor = '\0';
                }
            }
        }

        cursor = line;
        while (cursor < eol) {
            if (delimation == ASCII_WHITESPACE_DELIMATION) {
                // Move the cursor to the beginning of the next word.
                for (; !(*cursor); cursor++) {
                    if (cursor >= eol) {
                        goto next_line;
                    }
                }
            } else if (*cursor == '\0') {
                break;
            }

            // Attempt to open the path represented by the input, verify that
            // the path is not a folder and set the QUERY_FILENAME environment
            // variable.
            if ((input_fd = open(cursor, O_RDONLY)) == -1) {
                non_fatal_errors = 1;
                perror(cursor);
                if (delimation == ASCII_WHITESPACE_DELIMATION) {
                    goto next_word;
                }
                break;
            } else if (fstat(input_fd, &file_status) == -1) {
                perror(cursor);
                return 1;
            } else if (S_ISDIR(file_status.st_mode)) {
                non_fatal_errors = 1;
                fprintf(stderr, "%s: %s\n", cursor, strerror(EISDIR));
                if (delimation == ASCII_WHITESPACE_DELIMATION) {
                    goto next_word;
                }
                break;
            } else if (setenv("QUERY_FILENAME", cursor, 1) == -1) {
                perror("setenv");
                return 1;
            }

            switch ((child_pid = fork())) {
              case -1:
                perror("fork");
                return 1;

              case 0:
                // Replace the inherited stdin with the descriptor for the
                // queried file then exec the command.
                if ((dup2(input_fd, STDIN_FILENO) == -1) ||
                    (dup2(dev_null_fd, STDOUT_FILENO) == -1) ||
                    (dup2(errout_fd, STDERR_FILENO) == -1)) {

                    perror("dup2");
                    return 1;
                }
                execvp(argv[optind], &argv[optind]);
                perror(argv[optind]);
                kill(getppid(), SIGUSR1);
                return 1;

              default:
                close(input_fd);
            }

            // Wait on the child to exit, check its return code and display the
            // file name when the proper conditions are met.
            while (1) {
                if (wait(&status) == -1) {
                    perror("wait");
                    return 1;
                }

                if (WIFEXITED(status)) {
                    return_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    return_code = WTERMSIG(status) + 128;
                } else {
                    continue;
                }

                if ((display_on_success && return_code == EXIT_SUCCESS) ||
                   (!display_on_success && return_code != EXIT_SUCCESS)) {
                    if (delimation == NULL_BYTE_DELIMATION) {
                        fwrite(line, (size_t) line_length, 1, stdout);
                    } else {
                        puts(cursor);
                    }
                }

                break;
            }

            if (delimation != ASCII_WHITESPACE_DELIMATION) {
                break;
            }

next_word:
            // Move the cursor to the end of the current word.
            for (; *cursor; cursor++);
        }
    }

    return (non_fatal_errors ? 2 : 0);
}
