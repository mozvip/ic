/**
 * comic_loaders_pdf_poppler.c
 * PDF backend using poppler-utils CLI tools (pdftoppm, pdfinfo).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>

#include "pdf_backend.h"
#include "process_utils.h"

/* ── Poppler-specific context ─────────────────────────────────────── */

typedef struct PdfBackendContext {
    char *pdf_path;
    char *temp_dir;
    int   total_pages;
    int   pixel_density;   /* DPI passed to pdftoppm */
} PopplerContext;

#define POPPLER_DEFAULT_DPI 120

/* ── Helpers ──────────────────────────────────────────────────────── */

static int poppler_get_page_count(const char *path) {
    const char *args[] = {"pdfinfo", path, NULL};
    char *output = NULL;
    if (execute_process(args, true, &output) != 0 || !output) return -1;

    int n_pages = 0;
    char *line = strtok(output, "\n");
    while (line) {
        if (strncmp(line, "Pages:", 6) == 0) {
            sscanf(line + 6, "%d", &n_pages);
            break;
        }
        line = strtok(NULL, "\n");
    }
    SDL_free(output);
    return n_pages;
}

/* ── is_available ─────────────────────────────────────────────────── */

static bool poppler_is_available(void) {
    /* Check that pdfinfo and pdftoppm are reachable. */
    const char *args[] = {"pdfinfo", "-v", NULL};
    char *out = NULL;
    if (execute_process(args, true, &out) == 0 && out) {
        SDL_free(out);
        return true;
    }
    return false;
}

/* ── open ─────────────────────────────────────────────────────────── */

static PdfBackendContext* poppler_open(const char *pdf_path,
                                        const char *temp_dir,
                                        int *page_count) {
    int n = poppler_get_page_count(pdf_path);
    if (n <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Poppler: could not determine page count for %s", pdf_path);
        return NULL;
    }

    PopplerContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->pdf_path      = strdup(pdf_path);
    ctx->temp_dir      = strdup(temp_dir);
    ctx->total_pages   = n;
    ctx->pixel_density = POPPLER_DEFAULT_DPI;
    *page_count = n;
    return (PdfBackendContext*)ctx;
}

/* ── render_page ──────────────────────────────────────────────────── */

static bool poppler_render_page(PdfBackendContext *opaque, int page_index,
                                 int total_pages, char **out_path) {
    PopplerContext *ctx = (PopplerContext*)opaque;
    if (!ctx || !out_path) return false;

    /* Build output prefix & expected path (matches pdftoppm naming). */
    char prefix[512];
    snprintf(prefix, sizeof(prefix), "%s/page", ctx->temp_dir);

    int digits = total_pages > 99 ? 3 : total_pages > 9 ? 2 : 1;
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%0*d", digits, page_index + 1);

    char expected[512];
    int expected_len = snprintf(expected, sizeof(expected), "%s-%s.jpg", prefix, numbuf);
    if (expected_len < 0 || (size_t)expected_len >= sizeof(expected)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Poppler: output path too long for page %d", page_index + 1);
        return false;
    }

    /* Cache: if the file already exists, return immediately. */
    if (access(expected, F_OK) == 0) {
        *out_path = strdup(expected);
        return true;
    }

    /* Render the page with pdftoppm. */
    char page_str[12], density_str[12];
    snprintf(page_str,    sizeof(page_str),    "%d", page_index + 1);
    snprintf(density_str, sizeof(density_str), "%d", ctx->pixel_density);

    const char *args[] = {
        "pdftoppm",
        "-r", density_str,
        "-f", page_str,
        "-l", page_str,
        "-jpeg",
        ctx->pdf_path,
        prefix,
        NULL
    };
    execute_process(args, false, NULL);

    if (access(expected, F_OK) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Poppler: pdftoppm did not produce %s", expected);
        return false;
    }

    *out_path = strdup(expected);
    return true;
}

/* ── close ────────────────────────────────────────────────────────── */

static void poppler_close(PdfBackendContext *opaque) {
    PopplerContext *ctx = (PopplerContext*)opaque;
    if (!ctx) return;
    free(ctx->pdf_path);
    free(ctx->temp_dir);
    free(ctx);
}

/* ── Vtable ───────────────────────────────────────────────────────── */

const PdfBackendOps pdf_backend_poppler = {
    .name         = "Poppler (CLI)",
    .is_available = poppler_is_available,
    .open         = poppler_open,
    .render_page  = poppler_render_page,
    .close        = poppler_close,
};
