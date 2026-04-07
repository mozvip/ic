#include "upscale.h"
#include "image_loader.h"
#include "process_utils.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char* pick_exe(const char* exe_path) {
    const char* env = getenv("REALESRGAN_EXE");
    if (exe_path && *exe_path) return exe_path;
    if (env && *env) return env;
    return "realesrgan-ncnn-vulkan"; // must be in PATH
}

SDL_Surface* upscale(const char* input_path, int scale, const char* model, const char* exe_path) {
    if (!input_path || !*input_path) return NULL;
    if (scale <= 0) scale = 4;
    if (!model || !*model) model = "realesrgan-x4plus";

    const char* exe = pick_exe(exe_path);

    // Create a unique temp output path
    char out_path[PATH_MAX];
    snprintf(out_path, sizeof(out_path), "/tmp/ic_reup_%ld_%d.png", (long)time(NULL), (int)getpid());

    char scale_str[12];
    snprintf(scale_str, sizeof(scale_str), "%d", scale);

    const char *args[] = {
        exe,
        "-n", model,
        "-s", scale_str,
        "-i", input_path,
        "-o", out_path,
        NULL
    };

    int rc = execute_process(args, false, NULL);
    if (rc != 0) {
        SDL_Log("RealESRGAN command failed (%d)", rc);
        return NULL;
    }

    // Load resulting image
    SDL_Surface *surface = image_load_surface(out_path, NULL);
    if (!surface) {
        SDL_Log("Failed to load upscaled output: %s", out_path);
        return NULL;
    }

    // delete the temp file after loading
    unlink(out_path);
    return surface;
}