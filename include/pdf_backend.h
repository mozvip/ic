/**
 * pdf_backend.h
 * Pluggable PDF rendering backend interface.
 *
 * Each backend implements the PdfBackendOps vtable.  The active backend is
 * selected via viewer.pdf_backend (PdfBackendType enum defined in
 * comic_viewer.h) and can be changed at runtime through the ImGui UI.
 */

#ifndef PDF_BACKEND_H
#define PDF_BACKEND_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared opaque context – each backend defines its own struct. */
typedef struct PdfBackendContext PdfBackendContext;

/**
 * Operations that every PDF backend must implement.
 */
typedef struct PdfBackendOps {
    /** Human-readable backend name (e.g. "Poppler (CLI)", "MuPDF"). */
    const char *name;

    /** Return true when the backend's runtime dependencies are satisfied. */
    bool (*is_available)(void);

    /**
     * Open a PDF file and return a backend context.
     *
     * @param pdf_path   Absolute path to the PDF file.
     * @param temp_dir   Writable temp directory for intermediate files.
     * @param page_count Receives the total number of pages.
     * @return Opaque context, or NULL on failure.
     */
    PdfBackendContext* (*open)(const char *pdf_path, const char *temp_dir,
                               int *page_count);

    /**
     * Render a single page and return its file path.
     *
     * Implementations should cache: if the output file already exists the
     * function must return immediately instead of re-rendering.
     *
     * @param ctx         Backend context returned by open().
     * @param page_index  Zero-based page index.
     * @param total_pages Total pages (used for zero-padded filenames).
     * @param out_path    Receives a newly-allocated path string (caller frees).
     * @return true on success.
     */
    bool (*render_page)(PdfBackendContext *ctx, int page_index,
                        int total_pages, char **out_path);

    /** Close the context and free all backend resources. */
    void (*close)(PdfBackendContext *ctx);
} PdfBackendOps;

/* ── Backend registry helpers ─────────────────────────────────────────── */

/** Return the ops table for the given backend index. */
const PdfBackendOps* pdf_backend_get_ops(int type);

/** Return the number of compiled-in backends. */
int pdf_backend_get_count(void);

/** Convenience: get backend name for index. */
const char* pdf_backend_get_name(int type);

/** Convenience: check backend availability for index. */
bool pdf_backend_is_available(int type);

/* ── Concrete backend declarations ─────────────────────────────────── */
extern const PdfBackendOps pdf_backend_poppler;
extern const PdfBackendOps pdf_backend_mupdf;

#ifdef __cplusplus
}
#endif

#endif /* PDF_BACKEND_H */
