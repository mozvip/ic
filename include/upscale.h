#pragma once
#include <SDL3/SDL.h>

// Upscales an input file with RealESRGAN (ncnn Vulkan CLI).
// - input_path: source image file on disk
// - scale: usually 4 for RealESRGAN_x4plus
// - model: "realesrgan-x4plus" or "realesrgan-x4plus-anime"
// - exe_path: NULL to use "realesrgan-ncnn-vulkan" from PATH or env REALESRGAN_EXE
// Returns a new SDL_Surface* on success (caller owns and must SDL_DestroySurface).
SDL_Surface* upscale(const char* input_path, int scale, const char* model, const char* exe_path);

// Simple heuristic: should we upscale to target_w?
// Returns true if surface height is significantly smaller than target_h.
static inline bool should_upscale(int surface_h, int target_h) {
    return surface_h < target_h;
}