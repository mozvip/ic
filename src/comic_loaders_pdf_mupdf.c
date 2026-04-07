/**
 * comic_loaders_pdf_mupdf.c
 * PDF backend using the MuPDF C library (libmupdf).
 *
 * Compiled only when HAVE_MUPDF is defined (set by meson when the library is
 * found).  When the library is absent a stub vtable is provided so the rest of
 * the code can still reference pdf_backend_mupdf without link errors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>

#include "pdf_backend.h"

#ifdef HAVE_MUPDF
/* ══════════════════════════════════════════════════════════════════════
 *  Real MuPDF implementation
 * ══════════════════════════════════════════════════════════════════════ */

#include <mupdf/fitz.h>

#define MUPDF_DEFAULT_DPI 120
#define MUPDF_BASE_DPI    72.0f

typedef struct PdfBackendContext {
    fz_context  *fz_ctx;
    fz_document *doc;
    char        *pdf_path;
    char        *temp_dir;
    int          total_pages;
    float        zoom;        /* scale factor = target_dpi / 72 */
} MupdfContext;

/* ── is_available ─────────────────────────────────────────────────── */

static bool mupdf_is_available(void) {
    return true;   /* If we are compiled with HAVE_MUPDF the lib is present. */
}

/* ── open ─────────────────────────────────────────────────────────── */

static PdfBackendContext* mupdf_open(const char *pdf_path,
                                      const char *temp_dir,
                                      int *page_count) {
    fz_context *fz_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!fz_ctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MuPDF: fz_new_context failed");
        return NULL;
    }

    fz_register_document_handlers(fz_ctx);

    fz_document *doc = NULL;
    fz_try(fz_ctx) {
        doc = fz_open_document(fz_ctx, pdf_path);
    }
    fz_catch(fz_ctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MuPDF: cannot open %s – %s", pdf_path,
                     fz_caught_message(fz_ctx));
        fz_drop_context(fz_ctx);
        return NULL;
    }

    MupdfContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        fz_drop_document(fz_ctx, doc);
        fz_drop_context(fz_ctx);
        return NULL;
    }

    ctx->fz_ctx   = fz_ctx;
    ctx->doc      = doc;
    ctx->pdf_path = strdup(pdf_path);
    ctx->temp_dir = strdup(temp_dir);
    ctx->zoom     = (float)MUPDF_DEFAULT_DPI / MUPDF_BASE_DPI;

    fz_try(fz_ctx) {
        ctx->total_pages = fz_count_pages(fz_ctx, doc);
    }
    fz_catch(fz_ctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MuPDF: cannot count pages – %s",
                     fz_caught_message(fz_ctx));
        free(ctx->pdf_path);
        free(ctx->temp_dir);
        free(ctx);
        fz_drop_document(fz_ctx, doc);
        fz_drop_context(fz_ctx);
        return NULL;
    }

    if (ctx->total_pages <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MuPDF: document has no pages");
        free(ctx->pdf_path);
        free(ctx->temp_dir);
        free(ctx);
        fz_drop_document(fz_ctx, doc);
        fz_drop_context(fz_ctx);
        return NULL;
    }

    *page_count = ctx->total_pages;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MuPDF: opened %s – %d pages (zoom %.2f)",
                pdf_path, ctx->total_pages, ctx->zoom);

    return (PdfBackendContext*)ctx;
}

/* ── render_page ──────────────────────────────────────────────────── */

static bool mupdf_render_page(PdfBackendContext *opaque, int page_index,
                               int total_pages, char **out_path) {
    MupdfContext *ctx = (MupdfContext*)opaque;
    if (!ctx || !out_path) return false;

    /* Build expected output path. */
    int digits = total_pages > 99 ? 3 : total_pages > 9 ? 2 : 1;
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%0*d", digits, page_index + 1);

    char expected[512];
    snprintf(expected, sizeof(expected), "%s/mupdf_page-%s.png",
             ctx->temp_dir, numbuf);

    /* Cache hit – file already rendered. */
    if (access(expected, F_OK) == 0) {
        *out_path = strdup(expected);
        return true;
    }

    /* Render with MuPDF. */
    fz_context  *fctx = ctx->fz_ctx;
    fz_page     *page = NULL;
    fz_pixmap   *pix  = NULL;
    bool ok = false;

    fz_try(fctx) {
        page = fz_load_page(fctx, ctx->doc, page_index);
        fz_matrix ctm = fz_scale(ctx->zoom, ctx->zoom);
        pix = fz_new_pixmap_from_page(fctx, page, ctm,
                                       fz_device_rgb(fctx), 0);
        fz_save_pixmap_as_png(fctx, pix, expected);
        ok = true;
    }
    fz_always(fctx) {
        if (pix)  fz_drop_pixmap(fctx, pix);
        if (page) fz_drop_page(fctx, page);
    }
    fz_catch(fctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MuPDF: failed to render page %d – %s",
                     page_index + 1, fz_caught_message(fctx));
        return false;
    }

    if (!ok) return false;

    *out_path = strdup(expected);
    return true;
}

/* ── close ────────────────────────────────────────────────────────── */

static void mupdf_close(PdfBackendContext *opaque) {
    MupdfContext *ctx = (MupdfContext*)opaque;
    if (!ctx) return;

    if (ctx->doc)   fz_drop_document(ctx->fz_ctx, ctx->doc);
    if (ctx->fz_ctx) fz_drop_context(ctx->fz_ctx);
    free(ctx->pdf_path);
    free(ctx->temp_dir);
    free(ctx);
}

/* ── Vtable ───────────────────────────────────────────────────────── */

const PdfBackendOps pdf_backend_mupdf = {
    .name         = "MuPDF",
    .is_available = mupdf_is_available,
    .open         = mupdf_open,
    .render_page  = mupdf_render_page,
    .close        = mupdf_close,
};

#else /* !HAVE_MUPDF */
/* ══════════════════════════════════════════════════════════════════════
 *  Stub – compiled when libmupdf is not available
 * ══════════════════════════════════════════════════════════════════════ */

static bool stub_is_available(void)   { return false; }
static PdfBackendContext* stub_open(const char *p, const char *t, int *c) {
    (void)p; (void)t; (void)c;
    return NULL;
}
static bool stub_render(PdfBackendContext *c, int i, int n, char **o) {
    (void)c; (void)i; (void)n; (void)o;
    return false;
}
static void stub_close(PdfBackendContext *c) { (void)c; }

const PdfBackendOps pdf_backend_mupdf = {
    .name         = "MuPDF (not installed)",
    .is_available = stub_is_available,
    .open         = stub_open,
    .render_page  = stub_render,
    .close        = stub_close,
};

#endif /* HAVE_MUPDF */
