# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`ic` — Image Comic Viewer: a single-binary Linux desktop comic/image reader written in C11
(plus a little C++ for the Dear ImGui layer), built on SDL3. It opens CBZ, CBR, PDF and plain
directories of images. Meson is the only build system (the `Makefile`-based CI workflows in
`.github/workflows/` are stale leftovers and do not reflect the current build).

## Build, run, install

```bash
meson setup builddir && meson compile -C builddir   # debug build (default buildtype)
./builddir/ic <comic-file-or-directory>             # run
./builddir/ic /tmp                                  # quick smoke test: opens the file browser
./install.sh                                        # release build + system install (uses sudo)
./scripts/build-appimage.sh                         # AppImage (forces -Dimage_backend=sdl_image)
```

After a system upgrade (`pacman -Syu`), ninja may fail with `'/usr/lib/libX.so.N.M' ... missing`:
meson hard-codes versioned library paths, so run `meson setup --reconfigure <builddir>`
(or `--wipe`). `meson.build` explicitly links `libjpegxr` to work around Arch's `jxrlib`
package missing its `NEEDED` entry (link errors on `WMPAlloc`, `calloc_lim`…).

`builddir/` is the conventional dev build directory (`builddir-release/`, `build/` also exist
locally; all are gitignored). Configure option: `-Dimage_backend=auto|freeimage|sdl_image`.

Sample files for manual testing live in [test/](test/) (gitignored, present locally):
`test/test.cbz`, `test/test.cbr`, `test/test.pdf`, `test/images/`.

## Dependencies

Build-time required: `sdl3`, `SDL3_ttf`, `libzip`, `libm`, a C++ compiler, and the `imgui`
Meson subproject (wrap-git, ImGui v1.91.4 — fetched into `subprojects/imgui/`).

Optional, detected at configure time and gated behind `-D` defines:

| Dependency | Define | Effect when missing |
|---|---|---|
| FreeImage | `HAVE_FREEIMAGE` | falls back to SDL3_image or Imlib2 for decoding + no EXIF |
| SDL3_image | `HAVE_SDL_IMAGE3` | FreeImage and/or Imlib2 only |
| Imlib2 | `HAVE_IMLIB2` | FreeImage and/or SDL3_image only |
| MuPDF (`mupdf`, `mupdf-third`) | `HAVE_MUPDF` | only the Poppler CLI PDF backend exists |
| `glslc` | `HAVE_GPU_SHADERS` | GPU pixel shaders unavailable; CPU fallback used |

Runtime CLI tools (only needed for specific formats/features): `unrar` (CBR),
`pdfinfo` + `pdftoppm` from poppler-utils (Poppler PDF backend),
`realesrgan-ncnn-vulkan` (upscaling, override path with `REALESRGAN_EXE`).

GLSL shaders in [shaders/](shaders/) are compiled to SPIR-V by `glslc` at build time and
embedded into a generated `gpu_shaders_embed.h` by [scripts/embed_spv.py](scripts/embed_spv.py).
Adding a shader means editing both `shader_specs` in [meson.build](meson.build) and the
pipeline table in [src/gpu_shader.c](src/gpu_shader.c).

## Architecture

Flow: CLI parse ([src/main.c](src/main.c)) → `comic_viewer_init()` → `comic_viewer_load()`
(or `file_browser_open()` when no/invalid path) → `comic_viewer_run()` main loop →
`comic_viewer_cleanup()`.

All runtime state lives in one global, `struct ViewerState viewer`, declared in
[include/comic_viewer.h](include/comic_viewer.h) and defined in
[src/comic_viewer.c](src/comic_viewer.c). `ImageProcessingOptions *options` is a second
global (enhancement settings), allocated in `comic_viewer_run()`.

### Views, not pages

The unit of display is an `ImageView`: a node in a doubly-linked list holding 1–2 (up to
`MAX_IMAGES_PER_VIEW`) image indices, a combined `SDL_Surface`, an `SDL_Texture`, a crop rect
and the dominant left/right edge colors used by the overlay. `1` / `2` keys split and merge
view nodes at runtime, so the list mutates during a session. `viewer.images[]` (capacity
`MAX_IMAGES` = 1000) is the flat page array behind it.

### Modules

| Area | Files |
|---|---|
| Main loop, state, input, view list | [src/comic_viewer.c](src/comic_viewer.c) (~2300 lines — the big one) |
| Format dispatch | [src/comic_loaders.c](src/comic_loaders.c) — `archive_open()` routes by type |
| CBZ | [src/comic_loaders_cbz.c](src/comic_loaders_cbz.c) — libzip, on-demand extract |
| CBR | [src/comic_loaders_cbr.c](src/comic_loaders_cbr.c) — `unrar` CLI |
| PDF | [src/comic_loaders_pdf.c](src/comic_loaders_pdf.c) + a vtable backend each: [_poppler](src/comic_loaders_pdf_poppler.c), [_mupdf](src/comic_loaders_pdf_mupdf.c) |
| Directories | [src/comic_loaders_dir.c](src/comic_loaders_dir.c) |
| Decode + EXIF | [src/image_loader.c](src/image_loader.c) / [src/image_loader.h](src/image_loader.h) (private header, not in `include/`) |
| Color/contrast enhancement, filters | [src/image_processor.c](src/image_processor.c) |
| Frame composition, HUD, crop overlay | [src/render.c](src/render.c) |
| Margin fill (gradient/stretched/ambilight/shader) | [src/overlay.c](src/overlay.c), [src/edges.c](src/edges.c) |
| Pixel shaders, CPU path | [src/pixel_shader.c](src/pixel_shader.c) |
| Pixel shaders, SDL_GPU/SPIR-V path | [src/gpu_shader.c](src/gpu_shader.c) |
| ImGui settings + EXIF panels | [src/imgui_layer.cpp](src/imgui_layer.cpp) |
| In-app file browser (ImGui) | [src/file_browser_imgui.cpp](src/file_browser_imgui.cpp) |
| Subprocess helper | [src/process_utils.c](src/process_utils.c) |
| Temp dir/file helpers | [src/temp_utils.c](src/temp_utils.c) |
| RealESRGAN upscaling | [src/upscale.c](src/upscale.c) — currently built but not wired to any caller |

### Pluggable backends

Two runtime-switchable backend families, both selectable from the ImGui panel (F1):

- **Image decode** — `ImageBackend` (auto / freeimage / sdl_image / imlib2) in `src/image_loader.h`.
  `auto` prefers Imlib2, then FreeImage and SDL3_image. CLI: `--image-backend`.
- **PDF render** — `PdfBackendOps` vtable in [include/pdf_backend.h](include/pdf_backend.h)
  (`name`, `is_available`, `open`, `render_page`, `close`). Adding a backend means a new
  `const PdfBackendOps` instance, an entry in the registry in `comic_loaders_pdf.c`, and a
  value in `PdfBackendType`. `render_page` implementations must cache: return immediately if
  the output file already exists.

### Threading model — read this before touching page loading

Decoding happens on a worker thread (`load_view_surfaces_in_thread`); texture creation must
happen on the main thread, so the worker posts a custom SDL event
(`viewer.preload_event_type`) carrying a `PreloadResult`.

Cancellation uses a generation counter: `viewer.load_generation` (`SDL_AtomicInt`). The worker
re-checks its captured `generation` against the atomic at every step and bails out via
`cleanup_load_thread_resources()` if it is stale. Additionally, `view_changed()` and
`load_view()` **`SDL_WaitThread` on the previous load thread** before starting a new one —
this join is deliberate and guards against use-after-free when pages are turned rapidly while
the worker is still touching `viewer.images[]` (see commit `993f5ce`). Do not remove it in
favor of generation checks alone.

Archive access from multiple threads is serialized with `ArchiveHandle::archive_mutex`.

### Persistence and temp files

- Per-comic reading position (view index, zoom, pan) is saved to
  `$(SDL_GetPrefPath("mozvip","ic"))/viewer_state.tsv`, one tab-separated line per realpath'd
  source, rewritten via a `.tmp` file + `rename()`. Written on `comic_viewer_cleanup()`.
- Extracted/rendered pages go under `$(SDL_GetPrefPath)/tmp` via `temp_utils_*` — **not**
  `/tmp` directly. Always use the `temp_utils` helpers for new temp paths.

## Conventions

- **Don't grow `comic_viewer.c`.** Rendering, overlays, shaders and the browser are already
  extracted; put new functionality in a focused module with its header in `include/`.
- **Never call `system()`.** External commands go through `execute_process()` in
  [src/process_utils.c](src/process_utils.c), which wraps the SDL3 process API and takes an
  argv array (no shell quoting hazards).
- Public headers live in `include/` and wrap declarations in `extern "C"` guards, because the
  ImGui layer and file browser are C++ and include them.
- Anything touched from a worker thread must be synchronized or generation-guarded (see above).
- `warning_level=2` is on; keep new code warning-clean.
- After changing decode/processing behaviour, `comic_viewer_reload_current_view()` (or
  `unload_view()` + `load_view()`) is how the visible page is refreshed in place.

## Controls (for manual verification)

Navigation: `←`/`↑`/`Backspace` prev, `→`/`↓`/`Space` next, `Home`/`End`, mouse wheel.
`Enter` opens the file browser (or applies a crop), `Esc` cancels crop / quits, `Delete`
removes the current view — in directory mode it instead **erases the file(s) from disk**
(press twice within 3s to confirm; refused on the last remaining view). `1`/`2` single/double page. `F`/`F11`/`F12` fullscreen. `Z`, `+`/`-`,
left-click zoom; middle-drag then `Enter` to crop. `O` cycles overlay mode, `Shift+O` overlay
shader, `P` view shader, `S` scale mode, `R` border removal, `X` color filter, `E`
enhancements, `B`/`C`/`G` (± `Shift`) brightness/contrast/gamma, `Ctrl+Shift+C` copy view.
`F1` ImGui settings panel, `I` image/EXIF info. Gamepad shoulder buttons page.

## Known stale artifacts

- [.github/workflows/c-cpp.yml](.github/workflows/c-cpp.yml) and
  [release.yml](.github/workflows/release.yml) build with `make` and feature flags
  (`curl=`, `exif=`, `xinerama=`) that no longer exist — CI does not currently build this project.
- [README.md](README.md) links to `MESON_BUILD.md`, which is absent.
- [.github/copilot-instructions.md](.github/copilot-instructions.md) predates several
  refactors (it names `src/file_browser.c`, says FreeImage is required, and says temp files
  go to `/tmp/ic_viewer_*`). Prefer this file.
