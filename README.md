# IC - Image Comic Viewer

A Linux desktop reader for comic archives, PDF documents, and image folders,
built with C, SDL3, and Dear ImGui.

## Features

- Open CBZ/ZIP, CBR/RAR, PDF, image folders, or an individual image.
- Browse files in-app, with no path required at startup.
- Read single pages or combine adjacent pages into a spread.
- Load images on a worker thread and preload neighboring views.
- Use fullscreen, HiDPI displays, multiple monitors, and keyboard, mouse, or gamepad navigation.
- Zoom, crop the displayed view, remove white borders, and adjust image enhancement settings.
- Fill page margins with gradient, stretched, ambilight, or shader backgrounds.
- Apply color filters and pixel shaders, and copy the current view to the clipboard.
- Restore a saved reading position, including zoom and pan, for each source.
- Open files from the desktop or by dropping them onto the viewer.
- Use native, screen-reader-accessible reading controls with `F2` or `--accessible`.
- Move directory images to Trash by default, with explicit permanent deletion available.

## Install

### Download a CI Build

Open [GitHub Actions](https://github.com/mozvip/ic/actions/workflows/c-cpp.yml),
select a successful **Build Linux Binary and AppImage** run, and download an artifact.
Downloading workflow artifacts requires signing in to GitHub.

| Artifact | Contents |
| --- | --- |
| `ic-linux-x86_64-AppImage` | AppImage using SDL_image, with bundled application libraries |
| `ic-linux-x86_64-auto` | Native binary archive with automatic image-backend selection |
| `ic-linux-x86_64-sdl_image` | Native binary archive using SDL_image |

For the AppImage, unzip the downloaded artifact, then run:

```sh
chmod +x IC-*.AppImage
./IC-*.AppImage
```

If FUSE is unavailable, run it in extract-and-run mode:

```sh
APPIMAGE_EXTRACT_AND_RUN=1 ./IC-*.AppImage
```

Native artifacts contain a `.tar.gz` archive. Unzip the artifact, extract that
archive, and run `./ic`. Native binaries require the corresponding system runtime
libraries; they do not bundle dependencies.

CI builds target Linux x86_64 on Ubuntu 22.04, with a glibc 2.35 baseline and
pinned SDL3 libraries built from source. AppImages bundle application libraries;
older glibc versions are unsupported, and graphics-driver compatibility still
depends on the host. The baseline builds use CPU shaders because `glslc` is optional. PDF and CBR
command-line tools are not bundled; see [Runtime Dependencies](#runtime-dependencies).

Tagged `v*` releases publish both an AppImage and a native binary archive on the
[Releases page](https://github.com/mozvip/ic/releases).

### Build from Source

Required tools are a C11 compiler, a C++ compiler, Git, Meson, Ninja, pkg-config,
and CMake for dependency discovery, plus gettext and ImageMagick. Required libraries
are SDL3, SDL3_ttf, libzip, GIO/GLib 2.72 or newer, Fontconfig, GTK3,
and at least one image decoder: FreeImage, SDL3_image, or Imlib2. Meson downloads the pinned
Dear ImGui subproject on first configuration.

On Ubuntu 26.04:

```sh
sudo apt update
sudo apt install build-essential git cmake meson ninja-build pkg-config \
  libsdl3-dev libsdl3-ttf-dev libsdl3-image-dev libzip-dev libfreeimage-dev libimlib2-dev \
  libglib2.0-dev libfontconfig1-dev libgtk-3-dev gettext imagemagick
```

On Arch Linux:

```sh
sudo pacman -S --needed base-devel git cmake meson ninja pkgconf \
  sdl3 sdl3_ttf sdl3_image libzip freeimage imlib2 glib2 fontconfig gtk3 gettext imagemagick
```

Older distribution releases may not package SDL3. FreeImage can be omitted if
SDL3_image is installed. MuPDF enables an additional PDF renderer when detected;
`glslc` enables GPU shader compilation. CPU shader paths remain available without it.

```sh
git clone https://github.com/mozvip/ic.git
cd ic
meson setup builddir --buildtype=release
meson compile -C builddir
./builddir/ic
```

To select an image-backend policy, add one of these options to the initial
`meson setup` command:

| Option | Behavior |
| --- | --- |
| `-Dimage_backend=auto` | Default; prefers Imlib2, then FreeImage and SDL3_image |
| `-Dimage_backend=freeimage` | Requires FreeImage to be available |
| `-Dimage_backend=sdl_image` | Requires SDL3_image and excludes FreeImage |
| `-Dimage_backend=imlib2` | Requires Imlib2 to be available |

For an existing build directory, use `meson configure builddir -Dimage_backend=sdl_image`
instead of running the initial setup again.

To install for the current user without elevated privileges:

```sh
./install.sh --user
```

This installs to `~/.local`; ensure `~/.local/bin` is on `PATH`. A custom absolute
prefix is supported with `./install.sh --prefix /path/to/prefix`. The default
prefix is `/usr/local`, which requires running the installation as root; the script
never invokes `sudo` itself. It builds in `builddir-install` and installs the
launcher, hicolor icon, AppStream metadata, and translation catalogs alongside the binary.

After a system library upgrade, reconfigure the build if Ninja
reports a missing versioned library:

```sh
meson setup --reconfigure builddir
meson compile -C builddir
```

### Build an AppImage

Install the source-build dependencies, `desktop-file-utils`, AppStream,
`linuxdeploy`, its AppImage output plugin, and its GTK plugin. The packaging tools
must be available on `PATH`. The helper downloads all three linuxdeploy tools:

```sh
bash scripts/appimage-tools.sh
export PATH="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/ic-appimage-tools:$PATH"
./scripts/build-appimage.sh
```

The [packaging script](scripts/build-appimage.sh) creates a release build using
SDL_image, normalizes the application icon, and writes an `IC-*.AppImage` in the
repository root. It recreates its build and staging directories on every run.

```sh
VERSION=1.0.0 ./scripts/build-appimage.sh
```

`BUILD_DIR` and `APPDIR` override the default packaging directories. Packaging
defaults to `NO_STRIP=1` to avoid stripping failures with RELR-enabled libraries
and `APPIMAGE_EXTRACT_AND_RUN=1` so the tools work without FUSE.

## Usage

```sh
ic [options] [file-or-directory]
```

With no path, IC opens the file browser. An individual image opens its containing
folder and selects that image.

```sh
ic                              # Browse files
ic "my-comic.cbz"                # ZIP comic archive
ic "my-comic.cbr"                # RAR comic archive
ic "my-document.pdf"             # PDF document
ic ./my-images/                  # Image folder
ic ./my-images/page-12.png       # Start at a specific image
ic --monitor 1 "my-comic.cbz"     # Select a display by zero-based index
ic --image-backend sdl_image ./my-images/
ic --accessible "my-comic.cbz"   # Native reading controls
ic --version
ic -- "-leading-dash.cbz"        # Stop option parsing
```

Use `ic --help` to see command-line options. The image backend can also be selected
through `IC_IMAGE_BACKEND=auto|freeimage|sdl_image|imlib2` or the `F1` settings panel,
provided that backend was compiled in. Backgrounds are selectable with `O` or in
the settings panel. Unknown options, invalid monitor values, and multiple source
arguments are rejected. `--monitor -1` selects the primary display; nonnegative
values select a display by index.

### Controls

These controls apply while reading; the file browser and settings panel handle
their own keyboard and mouse input.

| Action | Control |
| --- | --- |
| Next page/view | Right, Down, Space, mouse wheel, right gamepad shoulder |
| Previous page/view | Left, Up, Backspace, mouse wheel, left gamepad shoulder |
| First / last view | Home / End |
| Open file browser | Enter |
| Fullscreen | F or F11 |
| Settings panel | F1 |
| Native reading controls | F2 |
| Image / EXIF information | I |
| Single-page / combine with next page | 1 / 2 |
| Toggle zoom | Z or left-click |
| Increase / decrease zoom while zoomed | + / - |
| Select a crop | Middle-button drag |
| Apply / cancel crop | Enter / Escape |
| Cycle margin background | O |
| Cycle background shader | Shift+O |
| Cycle page shader | P |
| Cycle scaling mode | S |
| Toggle automatic border removal | R |
| Cycle color filter | X |
| Toggle enhancements | E |
| Increase brightness / contrast / gamma | B / C / G |
| Decrease brightness / contrast / gamma | Shift+B / Shift+C / Shift+G |
| Copy current view to clipboard | Ctrl+Shift+C |
| Remove current view | Delete |
| Permanently delete directory images | Shift+Delete |
| Quit when not cropping | Escape |

In folder mode, Delete moves the current view's image files to the desktop Trash.
Press Delete twice within three seconds to confirm. Shift+Delete explicitly erases
files permanently and requires its own second Shift+Delete confirmation. Switching
between these modes resets confirmation. Trash failures never fall back to erasing
files. Deleting the last remaining view is refused.
In archive mode, Delete removes a view from the session without modifying the archive.
Cropping changes the displayed view; it does not rewrite the source image.

### Runtime Dependencies

| Feature | Requirement |
| --- | --- |
| CBZ / ZIP | libzip; bundled in the AppImage |
| CBR / RAR | `unrar` on `PATH` |
| PDF via Poppler | `pdfinfo` and `pdftoppm` from Poppler utilities |
| PDF via MuPDF | Optional compiled-in MuPDF backend |
| Text and interface labels | A system sans-serif font discovered through Fontconfig |
| Native controls and file chooser | GTK3 with the desktop's AT-SPI accessibility bridge |

Select the PDF renderer and rendering DPI in the `F1` settings panel. Image format
support depends on the compiled-in decoder. Sources support up to 1,000 images;
larger archives are rejected, and directory imports stop at that limit.

Reading position is saved on source changes and clean shutdown in
`$XDG_STATE_HOME/ic/viewer_state.tsv`, defaulting to `~/.local/state/ic/viewer_state.tsv`.
Existing state from SDL's old `mozvip/ic` preference directory is copied on first
use without deleting the original. Extracted/rendered pages live under
`$XDG_CACHE_HOME/ic`, defaulting to `~/.cache/ic`, and their owned directories are
removed when an archive closes. Cleanup does not follow symlinks outside those
directories. A crash can leave disposable cache files behind; the old preference
directory's legacy extraction cache is not migrated or removed automatically.

### Accessibility and Language

`F2` or `--accessible` opens a native GTK window with named AT-SPI buttons for
opening files, page navigation, zoom, fullscreen, Trash, and quitting, plus the
current source, page, and status. Its native file chooser supports keyboard and
screen-reader navigation. ImGui settings panels themselves are not AT-SPI
accessible, and IC does not provide OCR or descriptions of comic artwork.

Gettext catalogs follow the system locale. Initial French translations cover
native controls, browser actions, and selected settings labels; remaining text
still falls back to English. For an installed French UTF-8 locale, run:

```sh
LC_ALL=fr_FR.UTF-8 ic --accessible
```

Translation sources are in [po/fr.po](po/fr.po); extract new messages with
`meson compile -C builddir ic-pot`.

## Development

Meson is the only build system. For a debug build and regression tests:

```sh
meson setup builddir-debug --buildtype=debug
meson compile -C builddir-debug
meson test -C builddir-debug --print-errorlogs
```

For AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
meson setup builddir-asan --buildtype=debug -Db_sanitize=address,undefined
meson test -C builddir-asan --print-errorlogs
```

The [safety regression suite](tests/safety.c) uses SDL's dummy video driver and
covers loading, rendering geometry, navigation, processing, clipboard ownership,
and reading-position persistence. Both image decoders are tested when available.
It also checks XDG paths, state migration, symlink-safe cache cleanup, Trash
recovery metadata, confirmation modes, dropped files, and CLI validation. When
`xvfb-run` is available, non-sanitized builds additionally test native accessibility
roles, page/zoom actions, and French text; this test requires `fr_FR.UTF-8` to be
generated. CI installs Xvfb and generates that locale.

The [CI workflow](.github/workflows/c-cpp.yml) runs on pushes, pull requests, and
manual dispatches. It tests release builds with both backend policies and an
additional sanitizer build, then publishes native binaries and an SDL_image AppImage.
The [release workflow](.github/workflows/release.yml) publishes native binaries and
AppImages for `v*` tags. [scripts/ci-dependencies.sh](scripts/ci-dependencies.sh)
bootstraps the Ubuntu 22.04 container and pinned SDL libraries; it is intended for
disposable CI containers, not for installing dependencies on an arbitrary host.

Contributions are welcome through issues and pull requests. Keep changes focused,
add regression coverage for behavioral changes, and run the Meson tests before submitting.

## License and Credits

IC is licensed under [Apache-2.0](LICENSE).

Built with [SDL3](https://libsdl.org/), [Dear ImGui](https://github.com/ocornut/imgui),
[SDL_ttf](https://github.com/libsdl-org/SDL_ttf), [SDL_image](https://github.com/libsdl-org/SDL_image),
[FreeImage](https://freeimage.sourceforge.io/), and [libzip](https://libzip.org/).
PDF rendering uses [Poppler](https://poppler.freedesktop.org/) or
[MuPDF](https://mupdf.com/) when available.
