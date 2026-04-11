/**
 * comic_loaders_pdf.c
 * PDF loading front-end that dispatches to the active rendering backend
 * (Poppler CLI or MuPDF library).  The backend is selected via
 * viewer.pdf_backend before opening a PDF file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comic_loaders.h"
#include "pdf_backend.h"
#include "temp_utils.h"

/* ── Backend registry ─────────────────────────────────────────────── */

static const PdfBackendOps *backends[] = {
    &pdf_backend_poppler,
    &pdf_backend_mupdf,
};

#define BACKEND_COUNT ((int)(sizeof(backends) / sizeof(backends[0])))

const PdfBackendOps* pdf_backend_get_ops(int type) {
    if (type < 0 || type >= BACKEND_COUNT) return NULL;
    return backends[type];
}

int pdf_backend_get_count(void) {
    return BACKEND_COUNT;
}

const char* pdf_backend_get_name(int type) {
    const PdfBackendOps *ops = pdf_backend_get_ops(type);
    return ops ? ops->name : "Unknown";
}

bool pdf_backend_is_available(int type) {
    const PdfBackendOps *ops = pdf_backend_get_ops(type);
    if (!ops || !ops->is_available) {
        return false;
    }

    /*
     * Availability probes may call external tools (for example poppler's
     * pdfinfo). Cache the result so UI refreshes do not spawn child
     * processes every frame.
     */
    static bool cache_initialized = false;
    static signed char availability_cache[BACKEND_COUNT];

    if (!cache_initialized) {
        for (int i = 0; i < BACKEND_COUNT; i++) {
            availability_cache[i] = -1;
        }
        cache_initialized = true;
    }

    if (availability_cache[type] < 0) {
        availability_cache[type] = ops->is_available() ? 1 : 0;
    }

    return availability_cache[type] == 1;
}

/* ── Internal state stored in ArchiveHandle.archive_ptr ────────── */

typedef struct {
    const PdfBackendOps *ops;
    PdfBackendContext   *ctx;
} PdfState;

/* ── pdf_open ─────────────────────────────────────────────────────── */

ArchiveHandle* pdf_open(const char *path, int *total_images, ProgressCallback progress_cb) {
    if (progress_cb) progress_cb(0.0f, "Creating temp folder...");

    /* Create temp directory. */
    char temp_dir[512];
    if (!temp_utils_create_dir(temp_dir, sizeof(temp_dir), "ic_viewer_pdf_XXXXXX")) {
        if (progress_cb) progress_cb(1.0f, "Failed to create temporary directory");
        return NULL;
    }

    /* Select backend from viewer state. */
    int sel = (int)viewer.pdf_backend;
    const PdfBackendOps *ops = pdf_backend_get_ops(sel);
    if (!ops || !ops->is_available || !ops->is_available()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PDF backend '%s' not available, falling back to Poppler",
                    ops ? ops->name : "??");
        sel = PDF_BACKEND_POPPLER;
        ops = &pdf_backend_poppler;
    }

    if (progress_cb) progress_cb(0.2f, "Opening PDF...");

    int n_pages = 0;
    PdfBackendContext *bctx = ops->open(path, temp_dir, &n_pages);
    if (!bctx || n_pages <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PDF backend '%s' failed to open %s", ops->name, path);
        if (progress_cb) progress_cb(1.0f, "Failed to open PDF");
        return NULL;
    }

    if (progress_cb) progress_cb(0.5f, "Allocating handle...");

    ArchiveHandle *handle = calloc(1, sizeof(*handle));
    if (!handle) {
        ops->close(bctx);
        if (progress_cb) progress_cb(1.0f, "Memory allocation failed");
        return NULL;
    }

    /* Wrap backend state. */
    PdfState *state = calloc(1, sizeof(*state));
    state->ops = ops;
    state->ctx = bctx;

    handle->type         = ARCHIVE_TYPE_PDF;
    handle->path         = strdup(path);
    handle->archive_ptr  = state;
    handle->temp_dir     = strdup(temp_dir);
    handle->total_images = n_pages;
    handle->entry_names  = NULL;

    /* 1-to-1 page index mapping. */
    handle->page_indices = malloc(n_pages * sizeof(int));
    for (int i = 0; i < n_pages; i++) handle->page_indices[i] = i;

    *total_images = n_pages;

    if (progress_cb) {
        char msg[256];
        snprintf(msg, sizeof(msg), "PDF loaded with %d pages (%s)", n_pages, ops->name);
        progress_cb(1.0f, msg);
    }
    return handle;
}

/* ── pdf_get_image ────────────────────────────────────────────────── */

bool pdf_get_image(ArchiveHandle *handle, int index, char **out_path) {
    if (!handle || !out_path || index < 0 || index >= handle->total_images)
        return false;

    PdfState *state = (PdfState*)handle->archive_ptr;
    if (!state || !state->ops || !state->ctx) return false;

    int page_index = handle->page_indices[index];
    return state->ops->render_page(state->ctx, page_index,
                                    handle->total_images, out_path);
}

/* ── pdf_close ────────────────────────────────────────────────────── */

void pdf_close(ArchiveHandle *handle) {
    if (!handle) return;

    PdfState *state = (PdfState*)handle->archive_ptr;
    if (state) {
        if (state->ops && state->ctx) state->ops->close(state->ctx);
        free(state);
    }

    free(handle->page_indices);
    if (handle->temp_dir) free(handle->temp_dir);
    free(handle->path);
    free(handle);
}