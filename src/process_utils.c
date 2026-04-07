#include "process_utils.h"
#include <SDL3/SDL_process.h>
#include <stdio.h>
#include <stdlib.h>

int execute_process(const char **args, bool capture_output, char **out_output)
{
    if (out_output) {
        *out_output = NULL;
    }

    SDL_Process *process = SDL_CreateProcess(args, capture_output);
    if (!process)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateProcess failed: %s", SDL_GetError());
        return -1;
    }

    if (!capture_output)
    {
        int exitcode = -1;
        SDL_WaitProcess(process, true, &exitcode);
        SDL_DestroyProcess(process);
        return exitcode;
    }

    size_t datasize = 0;
    int exitcode = -1;
    char *output = (char *)SDL_ReadProcess(process, &datasize, &exitcode);
    SDL_DestroyProcess(process);

    if (!output)
    {
        return -1;
    }

    if (exitcode != 0)
    {
        SDL_free(output);
        return exitcode;
    }

    if (out_output)
    {
        *out_output = output;
    }
    else
    {
        SDL_free(output);
    }

    return exitcode;
}
