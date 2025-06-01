# Image Quality Enhancements

This document describes the image quality enhancement features implemented using the FreeImage library.

## Features Implemented

### 1. Auto Color Balance
- Corrects color casts using the gray world assumption
- Automatically balances red, green, and blue channels
- Limited to reasonable correction bounds (0.5x to 2.0x factor)

### 2. Histogram Stretching (Auto Levels)
- Automatically stretches image contrast for better dynamic range
- Finds minimum and maximum pixel values and maps them to full 0-255 range
- Improves visibility of details in dark or washed-out images

### 3. Gamma Correction
- Applies slight gamma correction (0.95) to improve contrast without over-brightening
- Makes images appear more natural and balanced while avoiding excessive brightness

### 4. Saturation Enhancement
- Slightly increases color saturation (1.05x) to make colors more vibrant
- Subtle enhancement that helps with dull or faded images without oversaturation

### 5. Automatic Enhancement
- Combines all the above techniques in an optimal sequence
- Enabled by default for all loaded images
- Can be toggled on/off with the **'E'** key during viewing

### 6. Interactive Control
- **'E' Key**: Toggle image enhancements on/off in real-time
- **'H' Key**: Display help with all keyboard shortcuts
- Visual indicator shows enhancement status: **[E+]** (on) or **[E-]** (off)

## Usage

### Keyboard Controls
- **'E'**: Toggle image enhancements on/off
- **'H'**: Show help with all available keyboard shortcuts
- Enhancement status is displayed in the page indicator: `1 / 10 [E+]`

### Automatic Enhancement (Default)
All images loaded through the comic viewer automatically receive quality enhancements.

### Manual Control
```c
#include "image_loader.h"

// Disable auto-enhancement
image_loader_set_auto_enhance(false);

// Re-enable auto-enhancement
image_loader_set_auto_enhance(true);
```

### Custom Processing
```c
#include "image_processor.h"

// Create custom processing options
ImageProcessingOptions options = get_default_processing_options();
options.gamma = 1.2;        // Stronger gamma correction
options.saturation = 1.3;   // More vibrant colors
options.color_balance = false; // Disable auto color balance

// Apply to a FreeImage bitmap
FIBITMAP* enhanced = process_image_quality(original_bitmap, &options);
```

## Technical Details

### Supported Image Formats
- JPEG, PNG, BMP, TIFF, GIF, ICO, WebP, TGA, PSD
- All formats supported by FreeImage library

### Performance
- Processing is applied during image loading
- Minimal impact on loading time
- Enhanced images are cached as SDL surfaces

### Memory Usage
- Temporary FreeImage bitmaps are created during processing
- All temporary memory is properly freed
- Final result is stored as SDL surface for rendering

## Quality Improvements for Different Image Types

### Scanned Comics/Documents
- **Auto Levels**: Improves contrast in scanned pages
- **Color Balance**: Corrects scanner color casts
- **Gamma**: Enhances text readability

### Digital Art/Photos
- **Saturation**: Makes colors more vibrant
- **Gamma**: Improves midtone detail
- **Auto Levels**: Optimizes dynamic range

### Poor Quality Images
- **Auto Levels**: Stretches limited contrast range
- **Color Balance**: Corrects color shifts
- **Gamma**: Improves visibility in dark areas

## Configuration

The enhancement system uses conservative settings that work well for most images:
- Gamma correction: 1.1 (subtle improvement)
- Saturation boost: 1.1 (10% increase)
- Color balance: Limited to 0.5x-2.0x correction range
- Auto levels: Full histogram stretching when beneficial

These settings provide noticeable improvement without over-processing or creating artifacts.
