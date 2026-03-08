#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <SDL3/SDL.h>
#include <stdbool.h>

// Executes a process and waits for it to finish.
// When capture_output is true, stdout is captured and returned via out_output.
// Captured output must be released with SDL_free by the caller.
// Returns the process exit code, or -1 if process creation/read fails.
int execute_process(const char **args, bool capture_output, char **out_output);

#endif // PROCESS_UTILS_H
