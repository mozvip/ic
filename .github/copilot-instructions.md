# Copilot instructions for `ic` (Image Comic Viewer)

## Big picture
- `ic` is a single C/SDL3 desktop app. The main runtime state is the global `viewer` (`struct ViewerState`) declared in [include/comic_viewer.h](include/comic_viewer.h) and defined in [src/comic_viewer.c](src/comic_viewer.c).
- Main flow: CLI parsing → `comic_viewer_init()` → `comic_viewer_load()` (optional) → `comic_viewer_run()` main loop. See [src/main.c](src/main.c).

## Module boundaries (where to make changes)
- Loading sources (CBZ/CBR/PDF/dir): `archive_open()` dispatches to format loaders in [src/comic_loaders*.c](src/) via APIs in [include/comic_loaders.h](include/comic_loaders.h).
  - CBZ uses `libzip` and extracts on-demand to `/tmp/ic_viewer_*` (thread-synchronized with `SDL_Mutex`).
  - CBR uses the `unrar` CLI via `SDL_CreateProcess` helpers in [src/process_utils.c](src/process_utils.c).
  - PDF uses `pdfinfo` + `pdftoppm` (poppler-utils) and renders pages to temp JPGs.
- Image decode + enhancement: FreeImage loads into `FIBITMAP` then creates an SDL surface in [src/image_loader.c](src/image_loader.c); optional auto-enhance logic lives in [src/image_processor.c](src/image_processor.c).
- Rendering: frame composition, overlays, and HUD are in [src/render.c](src/render.c) and [src/overlay.c](src/overlay.c). Overlay modes are `gradient|stretched|ambilight`.
- File browser (no path passed): interactive browser overlay in [src/file_browser.c](src/file_browser.c).
- Optional upscaling: [src/upscale.c](src/upscale.c) shells out to `realesrgan-ncnn-vulkan` (override with `REALESRGAN_EXE`).

## Build / run workflow (Meson)
- Configure + build (debug default): `meson setup builddir && meson compile -C builddir`
- Run locally during dev: use [./builddir/ic](./builddir/ic) → `./builddir/ic <comic-or-dir>`
- System install (release build): `./install.sh` (runs `meson install` and updates desktop database)

## Non-obvious external dependencies
- Required libs (build-time): `sdl3`, `SDL3_ttf`, `libzip`, `freeimage` (see [meson.build](meson.build)).
- Required CLIs for some formats:
  - CBR/RAR: `unrar`
  - PDF: `pdfinfo` + `pdftoppm` (poppler-utils)
- Fonts: `comic_viewer_init()` tries common system font paths; missing fonts are non-fatal.

## Project conventions (important when editing)
- Prefer adding functionality in the focused module rather than growing `comic_viewer.c`; rendering logic is already extracted into `render.c`.
- External commands should go through `execute_command*()` in [src/process_utils.c](src/process_utils.c) (uses SDL3 process API), not `system()`.
- `viewer` is global/shared state; anything touched from worker threads should be synchronized (pattern: CBZ archive uses `SDL_Mutex`).

## Quick manual smoke tests
- Use sample comics in [test/](test/) (e.g. `./builddir/ic test/test.cbz`) and verify:
  - Page turn + fullscreen + overlay cycle (`O`)
  - PDF open works when poppler-utils is installed
  - CBR open works when `unrar` is installed
