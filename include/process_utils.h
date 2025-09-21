#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <SDL3/SDL.h>
#include <stdbool.h>

// Executes a command and waits for it to finish.
// Returns the exit code of the process.
int execute_command(const char **args);

// Executes a command and captures its standard output.
// The output is returned as a newly allocated string that must be freed by the caller.
// Returns NULL if the command fails.
char *execute_command_with_output(const char **args);

#endif // PROCESS_UTILS_H
