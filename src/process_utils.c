#include "process_utils.h"
#include <SDL3/SDL_process.h>
#include <stdio.h>
#include <stdlib.h>

int execute_command(const char **args)
{
    SDL_Process *process = SDL_CreateProcess(args, false);
    if (!process)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateProcess failed: %s", SDL_GetError());
        return -1;
    }

    int exitcode = -1;
    SDL_WaitProcess(process, true, &exitcode);
    SDL_DestroyProcess(process);

    return exitcode;
}

char *execute_command_with_output(const char **args)
{
    SDL_Process *process = SDL_CreateProcess(args, true);
    if (!process)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateProcess failed: %s", SDL_GetError());
        return NULL;
    }

    size_t datasize = 0;
    int exitcode = -1;
    char *output = (char *)SDL_ReadProcess(process, &datasize, &exitcode);

    SDL_DestroyProcess(process);

    if (exitcode != 0)
    {
        SDL_free(output);
        return NULL;
    }

    return output;
}
