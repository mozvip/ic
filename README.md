# IC - Image Comic Viewer

IC (Image Comic Viewer) is a lightweight, fast comic book reader for Linux built with SDL3. It provides a smooth, high-quality reading experience for digital comics and image collections.

## Features

- **Multiple Format Support**: 
  - CBZ (ZIP) comic archives
  - CBR (RAR) comic archives
  - PDF documents (with some limitation)
  - Plain directories of images
- **High-Quality Rendering**:
  - Smooth image scaling with high-quality filtering
  - Adaptive background colors based on image content
  - HiDPI/4K display support
- **Performance Optimized**:
  - Smart image preloading for fast page turning
  - Memory-efficient operation for large comic files
  - Wayland compatible
- **User Friendly**:
  - Simple, distraction-free interface
  - Fullscreen mode (F12 or F key)
  - Multi-monitor support
  - Image navigation via keyboard or mouse wheel

## Requirements

- Linux operating system
- SDL3 libraries:
  - libsdl3
  - libsdl3-image
  - libsdl3-ttf

## Installation

### Building from Source

1. Clone the repository:
   ```
   git clone https://github.com/mozvip/ic.git
   cd ic
   ```

2. Ensure you have the required development libraries:
   ```
   # For Debian/Ubuntu
   sudo apt install libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev
   
   # For Fedora
   sudo dnf install SDL3-devel SDL3_image-devel SDL3_ttf-devel
   
   # For Arch Linux
   sudo pacman -S sdl3 sdl3_image sdl3_ttf
   ```

3. Build the project:
   ```
   make
   ```

## Usage

### Basic Usage

```
ic [OPTIONS] <comic-file-or-directory>
```

Where `<comic-file-or-directory>` can be:
- A .cbz or .zip file
- A .cbr or .rar file
- A directory containing images

### Controls

- **Next Page**: Right Arrow, Down Arrow, Space
- **Previous Page**: Left Arrow, Up Arrow, Backspace
- **First Page**: Home
- **Last Page**: End
- **Toggle Fullscreen**: F12 or F key
- **Exit Viewer**: Escape
- **Navigate**: Mouse wheel scrolling

## Example

```
# Open a CBZ comic
ic my-comic.cbz

# Open a directory of images
ic ./my-images/

# Open on a specific monitor (starting from 0)
ic --monitor 1 my-comic.cbz
```

## License

Apache Licence 2.0

## Contributing

Contributions welcome! Please feel free to submit a Pull Request.

## Acknowledgments

- SDL3 for providing the rendering capabilities
