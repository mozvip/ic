# Code Review TODO

## Linux Desktop Integration

- [x] Use XDG cache/state directories and migrate legacy reading position.
- [x] Remove extracted/rendered page directories on archive close without following symlinks.
- [x] Discover system fonts with Fontconfig.
- [x] Set a stable application ID and install matching desktop, hicolor icon, and AppStream metadata.
- [x] Register supported image MIME types and accept dropped files.
- [x] Use Trash by default and require separate Shift+Delete confirmation for permanent removal.
- [x] Add version reporting, strict CLI validation, and `--` support.
- [x] Support unprivileged and custom-prefix installation without implicit sudo.
- [x] Build against Ubuntu 22.04 and publish AppImages on tagged releases.
- [x] Add native AT-SPI reading controls and initial gettext/French translations.
- [ ] Extend translation coverage to all remaining settings, progress, and status text.
- [ ] Provide accessibility for the remaining ImGui settings and image-content descriptions.
- [ ] Consider safe crash-stale cache collection and explicit legacy-cache cleanup.

Review date: 2026-10-14. All nine high-priority and nine medium-priority findings
are implemented. Verification details and remaining manual checks are below.

## High Priority

- [x] H1: Prevent ZIP path traversal.
  ZIP entries previously controlled extraction paths and could escape the
  temporary directory using `../`. Extraction now uses generated page names
  with checked path lengths, while retaining image extensions. Nested entries
  and long archive names remain supported.
  Source: [src/comic_loaders_cbz.c](src/comic_loaders_cbz.c).

- [x] H2: Reject archive and PDF counts beyond the image-array capacity.
  Unbounded page counts could write past `viewer.images[MAX_IMAGES]`.
  The common archive dispatcher now rejects counts outside 1..1000 and closes
  rejected handles. Image preparation and unloading also check array bounds.
  Sources: [src/comic_loaders.c](src/comic_loaders.c),
  [src/comic_viewer.c](src/comic_viewer.c).

- [x] H3: Fix single-page surface ownership.
  A view previously aliased its backing image surface, causing double
  destruction during unload. Single-page views now own independent surfaces.
  Worker failure and completion paths no longer destroy published surfaces.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

- [x] H4: Keep clipboard cleanup from freeing live views.
  Clipboard callbacks previously retained viewer-owned nodes and cleanup freed
  them. Clipboard data now owns an encoded PNG snapshot, independent of page
  navigation, reloads, view deletion, and decoder shutdown. Replacement and
  cleanup free only that snapshot.
  Source: [src/clipboard.c](src/clipboard.c).

- [x] H5: Accept null MIME types in clipboard callbacks.
  SDL may invoke a clipboard callback with a null MIME type. The callback now
  checks its arguments before comparison and clears the output size safely.
  Source: [src/clipboard.c](src/clipboard.c).

- [x] H6: Finish workers and drain stale events before resource teardown.
  Browser source changes, view deletion, splitting/merging, source loading, and
  shutdown now join workers before freeing shared resources. The common helper
  reaps finished thread handles, drains all queued completions rather than only
  16, and advances the generation. Shutdown joins before decoder/archive/GPU
  teardown and before processing options are freed.
  Sources: [src/comic_viewer.c](src/comic_viewer.c),
  [src/file_browser_imgui.cpp](src/file_browser_imgui.cpp),
  [include/comic_viewer.h](include/comic_viewer.h).

- [x] H7: Synchronize reloads and worker-shared settings changes.
  Reloads previously unloaded surfaces before joining their loader. Keyboard
  and ImGui settings now finish loading before changing processing options,
  decoder selection, border settings, or view shaders. Unload itself also
  enforces the join-before-free ordering.
  Sources: [src/comic_viewer.c](src/comic_viewer.c),
  [src/imgui_layer.cpp](src/imgui_layer.cpp).

- [x] H8: Release the ZIP mutex on output-creation failures.
  Failed `fopen` previously returned while keeping the archive mutex locked.
  All extraction paths now release it. Read, write, and close failures also
  discard partial output so retries cannot mistake it for a valid cache entry.
  Source: [src/comic_loaders_cbz.c](src/comic_loaders_cbz.c).

- [x] H9: Do not let held Delete confirm destructive operations.
  OS key repeats previously counted as the second confirmation press and could
  erase successive files. Delete now ignores repeated keydown events while
  preserving the two-distinct-press confirmation.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

## Medium Priority

- [x] M1: Correct monitor selection. Display indexes now resolve through SDL's
  display list, bounds-query success is interpreted correctly, and `-1` selects
  the primary display. Invalid indexes are rejected.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

- [x] M2: Allow X11 fallback rather than forcing Wayland during initialization.
  SDL chooses the available video backend and honors environment overrides.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

- [x] M3: Preserve every page when merging views. Double-page mode consumes only
  the first page of the next node, retaining its remaining pages. `[A]` plus
  `[B,C]` becomes `[A,B]` followed by `[C]`.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

- [x] M4: Map crop selections using the actual displayed crop geometry, not
  full-texture geometry. Rendering and cropping share one source/destination
  calculation, including zoom, pan, and viewport clipping.
  Sources: [src/render.c](src/render.c), [include/render.h](include/render.h),
  [src/comic_viewer.c](src/comic_viewer.c).

- [x] M5: Make rendering and input coordinates consistent on HiDPI displays.
  Drawing uses window-logical coordinates and SDL logical presentation. SDL3
  high-pixel-density windows are enabled; resize/pixel-density changes are
  handled, the loading bar follows logical dimensions, and ImGui avoids double
  framebuffer scaling. Pixel tests cover 1x, 2x, and 3x output resolutions.
  Sources: [src/render.c](src/render.c), [src/comic_viewer.c](src/comic_viewer.c),
  [src/progress_bar.c](src/progress_bar.c), [src/imgui_layer.cpp](src/imgui_layer.cpp).

- [x] M6: Clamp channel values before fractional gamma correction. Negative
  intermediate values can no longer become invalid and produce white pixels.
  Source: [src/image_processor.c](src/image_processor.c).

- [x] M7: Guard empty-viewer navigation and split/merge keys. Keys 1, 2, and
  End now check their view pointers before access. Single-page splitting also
  leaves the existing node unchanged if allocation fails.
  Source: [src/comic_viewer.c](src/comic_viewer.c).

- [x] M8: Save the previous comic's reading position before switching sources,
  rather than only on final shutdown. The browser saves before teardown, and
  loading also saves before changing the source. Page, zoom, and pan round trips
  for two independent sources are tested.
  Sources: [src/file_browser_imgui.cpp](src/file_browser_imgui.cpp),
  [src/comic_viewer.c](src/comic_viewer.c).

- [x] M9: Replace obsolete Makefile-based CI/release workflows with Meson builds
  and safety tests. Ubuntu 26.04 containers install the required dependencies,
  including CMake for SDL3_ttf discovery. CI tests both decoder configurations
  and sanitizers. Releases build and test the actual binary before packaging;
  release notes explicitly identify unbundled runtime dependencies.
  Sources: [.github/workflows/c-cpp.yml](.github/workflows/c-cpp.yml),
  [.github/workflows/release.yml](.github/workflows/release.yml).

## Verification

- Full debug application build passed with both image decoders, MuPDF, and
  GPU shader compilation enabled.
- The Meson safety suite passed with auto/FreeImage and explicit SDL_image.
- Both cases also passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- An SDL_image-only release build and its tests passed locally.
- Ubuntu 26.04 container builds and tests passed for auto/FreeImage release,
  SDL_image-only debug, and auto with AddressSanitizer/UndefinedBehaviorSanitizer.
  ImGui was fetched from its wrap, and the release archive contained the binary,
  README, and license. Both workflows passed `actionlint`.
- The suite exercises ZIP containment, nested and long entry names, extraction
  failure/unlock/retry, the 1000/1001 image boundary, independent view surfaces,
  queued completions, processing reloads, split/merge, archive-view removal,
  clipboard replacement and reads after teardown, held Delete, and shutdown
  with a pending preload.
- Additional regressions check SDL display IDs and default/invalid monitor
  indexes, `[A]` plus `[B,C]` merging, empty-viewer keys, fractional gamma limits,
  cropped/zoomed/panned selection mapping, rendered page and loading-bar pixels
  at three densities, and independent reading-position saves/restores.
- Separate focused probes checked the shared count guard for CBZ, CBR, and PDF,
  nullable clipboard callbacks, and worker join-before-unload.
- Tests live in [tests/safety.c](tests/safety.c), registered in
  [meson.build](meson.build).

Reproduce in a new writable build directory:

```sh
meson setup builddir-safety
meson compile -C builddir-safety
meson test -C builddir-safety --print-errorlogs

meson setup builddir-safety-asan -Db_sanitize=address,undefined
meson test -C builddir-safety-asan --print-errorlogs
```

The existing `builddir` Ninja log was not writable by the current user, so
verification used fresh build directories under `/tmp` without changing
existing permissions. SDL tests use its dummy video driver and software-rendered
HiDPI canvases. Real desktop/browser interactions, multi-monitor hotplug, and
GPU rendering remain manual checks. The GitHub-hosted jobs and release upload
were not triggered; their build/test/package commands passed in the container.
Release builds report existing warnings in source-path copying, MuPDF longjmp
handling, and the browser's unchecked `getcwd`; those are outside these findings.