/* gpu_shader.c - SDL_GPU pixel shader runner
 *
 * One pipeline per built-in pixel shader. To apply a shader to an SDL_Surface
 * we upload it to a sampled SDL_GPUTexture, render a fullscreen triangle into
 * a color-target texture with the chosen fragment pipeline, and download the
 * result back into the surface. The cost is paid once per page load.
 */

#include "gpu_shader.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <string.h>

#ifdef HAVE_GPU_SHADERS
#include "gpu_shaders_embed.h"   /* generated header with SPIR-V byte arrays */
#endif

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    SDL_GPUGraphicsPipeline *pipeline;
    bool needs_uniforms;   /* shader uses set=3 fragment uniform buffer */
} GpuShaderEntry;

static struct {
    SDL_GPUDevice *device;
    bool owns_device;
    SDL_GPUShader *vertex_shader;
    SDL_GPUSampler *sampler;
    GpuShaderEntry entries[PIXEL_SHADER_COUNT];
    bool initialised;
} g;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#ifdef HAVE_GPU_SHADERS

static SDL_GPUShader *create_shader(SDL_GPUShaderStage stage,
                                    const uint8_t *code, size_t code_len,
                                    Uint32 num_samplers,
                                    Uint32 num_uniform_buffers) {
    SDL_GPUShaderCreateInfo info = {0};
    info.code = code;
    info.code_size = code_len;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = num_samplers;
    info.num_storage_textures = 0;
    info.num_storage_buffers = 0;
    info.num_uniform_buffers = num_uniform_buffers;
    return SDL_CreateGPUShader(g.device, &info);
}

static SDL_GPUGraphicsPipeline *make_pipeline(SDL_GPUShader *frag) {
    SDL_GPUColorTargetDescription color_target = {0};
    color_target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    color_target.blend_state.enable_blend = false;
    color_target.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUGraphicsPipelineCreateInfo pinfo = {0};
    pinfo.vertex_shader = g.vertex_shader;
    pinfo.fragment_shader = frag;
    pinfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pinfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pinfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pinfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pinfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pinfo.target_info.num_color_targets = 1;
    pinfo.target_info.color_target_descriptions = &color_target;
    pinfo.target_info.has_depth_stencil_target = false;

    return SDL_CreateGPUGraphicsPipeline(g.device, &pinfo);
}

/* Register a fragment shader and build its pipeline. */
static void register_shader(PixelShaderType type,
                            const uint8_t *code, size_t code_len,
                            Uint32 num_uniform_buffers) {
    SDL_GPUShader *fs = create_shader(SDL_GPU_SHADERSTAGE_FRAGMENT,
                                      code, code_len,
                                      1, num_uniform_buffers);
    if (!fs) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: failed to create fragment shader %d: %s",
                    (int)type, SDL_GetError());
        return;
    }
    SDL_GPUGraphicsPipeline *p = make_pipeline(fs);
    /* Pipeline holds a ref so we can release the shader. */
    SDL_ReleaseGPUShader(g.device, fs);
    if (!p) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: failed to build pipeline for %d: %s",
                    (int)type, SDL_GetError());
        return;
    }
    g.entries[type].pipeline = p;
    g.entries[type].needs_uniforms = (num_uniform_buffers > 0);
}

#endif /* HAVE_GPU_SHADERS */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool gpu_shader_init(SDL_Renderer *renderer) {
#ifndef HAVE_GPU_SHADERS
    (void)renderer;
    SDL_Log("gpu_shader: built without HAVE_GPU_SHADERS, CPU fallback only");
    return false;
#else
    if (g.initialised) return true;
    memset(&g, 0, sizeof(g));

    /* Prefer the renderer's GPU device when available so we don't double-up. */
    if (renderer) {
        SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
        SDL_GPUDevice *dev = (SDL_GPUDevice *)SDL_GetPointerProperty(
            props, SDL_PROP_RENDERER_GPU_DEVICE_POINTER, NULL);
        if (dev) {
            g.device = dev;
            g.owns_device = false;
        }
    }

    if (!g.device) {
        g.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
        g.owns_device = true;
    }
    if (!g.device) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: no SDL_GPU device available, CPU fallback only");
        return false;
    }

    g.vertex_shader = create_shader(SDL_GPU_SHADERSTAGE_VERTEX,
                                    ic_spv_fullscreen_vert,
                                    ic_spv_fullscreen_vert_len,
                                    0, 0);
    if (!g.vertex_shader) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: vertex shader create failed: %s", SDL_GetError());
        gpu_shader_shutdown();
        return false;
    }

    SDL_GPUSamplerCreateInfo sinfo = {0};
    sinfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sinfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sinfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sinfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sinfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sinfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g.sampler = SDL_CreateGPUSampler(g.device, &sinfo);
    if (!g.sampler) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: sampler create failed: %s", SDL_GetError());
        gpu_shader_shutdown();
        return false;
    }

    register_shader(PIXEL_SHADER_INVERT,
                    ic_spv_invert_frag, ic_spv_invert_frag_len, 0);
    register_shader(PIXEL_SHADER_SCANLINES,
                    ic_spv_scanlines_frag, ic_spv_scanlines_frag_len, 1);
    register_shader(PIXEL_SHADER_VIGNETTE,
                    ic_spv_vignette_frag, ic_spv_vignette_frag_len, 0);
    register_shader(PIXEL_SHADER_SEPIA,
                    ic_spv_sepia_frag, ic_spv_sepia_frag_len, 0);
    register_shader(PIXEL_SHADER_PIXELATE,
                    ic_spv_pixelate_frag, ic_spv_pixelate_frag_len, 1);
    register_shader(PIXEL_SHADER_MIRROR,
                    ic_spv_mirror_frag, ic_spv_mirror_frag_len, 0);

    g.initialised = true;
    SDL_Log("gpu_shader: ready (driver: %s, device %s)",
            SDL_GetGPUDeviceDriver(g.device),
            g.owns_device ? "own" : "shared");
    return true;
#endif
}

void gpu_shader_shutdown(void) {
#ifdef HAVE_GPU_SHADERS
    if (!g.device) {
        memset(&g, 0, sizeof(g));
        return;
    }
    for (int i = 0; i < PIXEL_SHADER_COUNT; i++) {
        if (g.entries[i].pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(g.device, g.entries[i].pipeline);
            g.entries[i].pipeline = NULL;
        }
    }
    if (g.sampler) {
        SDL_ReleaseGPUSampler(g.device, g.sampler);
        g.sampler = NULL;
    }
    if (g.vertex_shader) {
        SDL_ReleaseGPUShader(g.device, g.vertex_shader);
        g.vertex_shader = NULL;
    }
    if (g.owns_device) {
        SDL_DestroyGPUDevice(g.device);
    }
    memset(&g, 0, sizeof(g));
#endif
}

bool gpu_shader_is_available(void) {
#ifdef HAVE_GPU_SHADERS
    return g.initialised;
#else
    return false;
#endif
}

bool gpu_shader_has(PixelShaderType type) {
#ifdef HAVE_GPU_SHADERS
    if (!g.initialised) return false;
    if (type <= PIXEL_SHADER_NONE || type >= PIXEL_SHADER_COUNT) return false;
    return g.entries[type].pipeline != NULL;
#else
    (void)type;
    return false;
#endif
}

#ifdef HAVE_GPU_SHADERS

/* Convert an SDL_Surface into a tightly-packed RGBA buffer the GPU can ingest. */
static SDL_Surface *to_rgba32(SDL_Surface *src) {
    if (!src) return NULL;
    if (src->format == SDL_PIXELFORMAT_RGBA32) return src;
    return SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
}

bool gpu_shader_apply_to_surface(PixelShaderType type, SDL_Surface *surface) {
    if (!surface) return false;
    if (!g.initialised) return false;
    if (type <= PIXEL_SHADER_NONE || type >= PIXEL_SHADER_COUNT) return false;
    GpuShaderEntry *entry = &g.entries[type];
    if (!entry->pipeline) return false;

    int w = surface->w, h = surface->h;
    if (w <= 0 || h <= 0) return false;

    /* Always work in RGBA32: the pipelines were built for R8G8B8A8_UNORM. */
    SDL_Surface *rgba = to_rgba32(surface);
    if (!rgba) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "gpu_shader: convert to RGBA32 failed: %s", SDL_GetError());
        return false;
    }
    bool rgba_is_owned = (rgba != surface);

    bool ok = false;
    SDL_GPUTexture *src_tex = NULL, *dst_tex = NULL;
    SDL_GPUTransferBuffer *upload_xfer = NULL, *download_xfer = NULL;

    const size_t bytes = (size_t)w * (size_t)h * 4;

    SDL_GPUTextureCreateInfo src_info = {0};
    src_info.type = SDL_GPU_TEXTURETYPE_2D;
    src_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    src_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    src_info.width = (Uint32)w;
    src_info.height = (Uint32)h;
    src_info.layer_count_or_depth = 1;
    src_info.num_levels = 1;
    src_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    src_tex = SDL_CreateGPUTexture(g.device, &src_info);
    if (!src_tex) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: src texture: %s", SDL_GetError()); goto done; }

    SDL_GPUTextureCreateInfo dst_info = src_info;
    dst_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    dst_tex = SDL_CreateGPUTexture(g.device, &dst_info);
    if (!dst_tex) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: dst texture: %s", SDL_GetError()); goto done; }

    SDL_GPUTransferBufferCreateInfo up_info = {0};
    up_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    up_info.size = (Uint32)bytes;
    upload_xfer = SDL_CreateGPUTransferBuffer(g.device, &up_info);
    if (!upload_xfer) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: upload xfer: %s", SDL_GetError()); goto done; }

    SDL_GPUTransferBufferCreateInfo dn_info = up_info;
    dn_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    download_xfer = SDL_CreateGPUTransferBuffer(g.device, &dn_info);
    if (!download_xfer) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: download xfer: %s", SDL_GetError()); goto done; }

    /* Copy surface pixels into the upload transfer buffer, packed tightly. */
    {
        if (!SDL_LockSurface(rgba)) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "gpu_shader: lock surface: %s", SDL_GetError()); goto done; }
        Uint8 *map = SDL_MapGPUTransferBuffer(g.device, upload_xfer, false);
        if (!map) {
            SDL_UnlockSurface(rgba);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "gpu_shader: map upload: %s", SDL_GetError());
            goto done;
        }
        const Uint8 *src_pixels = (const Uint8 *)rgba->pixels;
        size_t row_bytes = (size_t)w * 4;
        for (int y = 0; y < h; y++) {
            memcpy(map + y * row_bytes, src_pixels + y * rgba->pitch, row_bytes);
        }
        SDL_UnmapGPUTransferBuffer(g.device, upload_xfer);
        SDL_UnlockSurface(rgba);
    }

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g.device);
    if (!cmd) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: acquire cmd: %s", SDL_GetError()); goto done; }

    /* Upload pass: transfer buffer -> sampled texture. */
    {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo tinfo = {0};
        tinfo.transfer_buffer = upload_xfer;
        tinfo.offset = 0;
        tinfo.pixels_per_row = (Uint32)w;
        tinfo.rows_per_layer = (Uint32)h;
        SDL_GPUTextureRegion tregion = {0};
        tregion.texture = src_tex;
        tregion.w = (Uint32)w;
        tregion.h = (Uint32)h;
        tregion.d = 1;
        SDL_UploadToGPUTexture(cp, &tinfo, &tregion, false);
        SDL_EndGPUCopyPass(cp);
    }

    /* Render pass: shader writes into dst_tex. */
    {
        SDL_GPUColorTargetInfo cti = {0};
        cti.texture = dst_tex;
        cti.load_op = SDL_GPU_LOADOP_DONT_CARE;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        cti.cycle = false;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &cti, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, entry->pipeline);
        SDL_GPUTextureSamplerBinding tsb = {0};
        tsb.texture = src_tex;
        tsb.sampler = g.sampler;
        SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
        if (entry->needs_uniforms) {
            /* std140 layout for: vec2 image_size; float time;  (16 bytes) */
            float params[4] = {
                (float)w,
                (float)h,
                (float)SDL_GetTicks() * 0.001f,
                0.0f
            };
            SDL_PushGPUFragmentUniformData(cmd, 0, params, sizeof(params));
        }
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }

    /* Download pass: dst_tex -> download transfer buffer. */
    {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion tregion = {0};
        tregion.texture = dst_tex;
        tregion.w = (Uint32)w;
        tregion.h = (Uint32)h;
        tregion.d = 1;
        SDL_GPUTextureTransferInfo tinfo = {0};
        tinfo.transfer_buffer = download_xfer;
        tinfo.offset = 0;
        tinfo.pixels_per_row = (Uint32)w;
        tinfo.rows_per_layer = (Uint32)h;
        SDL_DownloadFromGPUTexture(cp, &tregion, &tinfo);
        SDL_EndGPUCopyPass(cp);
    }

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "gpu_shader: submit: %s", SDL_GetError()); goto done; }
    SDL_WaitForGPUFences(g.device, true, &fence, 1);
    SDL_ReleaseGPUFence(g.device, fence);

    /* Copy the downloaded result back into the surface. */
    {
        const Uint8 *map = SDL_MapGPUTransferBuffer(g.device, download_xfer, false);
        if (!map) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "gpu_shader: map download: %s", SDL_GetError()); goto done; }

        if (!SDL_LockSurface(rgba)) {
            SDL_UnmapGPUTransferBuffer(g.device, download_xfer);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "gpu_shader: lock rgba: %s", SDL_GetError());
            goto done;
        }
        Uint8 *dst_pixels = (Uint8 *)rgba->pixels;
        size_t row_bytes = (size_t)w * 4;
        for (int y = 0; y < h; y++) {
            memcpy(dst_pixels + y * rgba->pitch, map + y * row_bytes, row_bytes);
        }
        SDL_UnlockSurface(rgba);
        SDL_UnmapGPUTransferBuffer(g.device, download_xfer);
    }

    /* If we converted, blit the result back into the original surface. */
    if (rgba_is_owned) {
        if (!SDL_BlitSurface(rgba, NULL, surface, NULL)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "gpu_shader: blit back: %s", SDL_GetError());
            goto done;
        }
    }

    ok = true;

done:
    if (rgba_is_owned && rgba) SDL_DestroySurface(rgba);
    if (upload_xfer)   SDL_ReleaseGPUTransferBuffer(g.device, upload_xfer);
    if (download_xfer) SDL_ReleaseGPUTransferBuffer(g.device, download_xfer);
    if (src_tex)       SDL_ReleaseGPUTexture(g.device, src_tex);
    if (dst_tex)       SDL_ReleaseGPUTexture(g.device, dst_tex);
    return ok;
}

#else /* !HAVE_GPU_SHADERS */

bool gpu_shader_apply_to_surface(PixelShaderType type, SDL_Surface *surface) {
    (void)type; (void)surface;
    return false;
}

#endif
