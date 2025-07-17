# PIT - Phono in Terminal

<p align="center">
  <img src="logo/pit_logo.png" alt="Project Logo" width="250"/>
</p>

---

PIT (Phono in Terminal) is a lightweight, platform-independent image viewer for terminal environments. It renders PNG and JPEG images using ANSI escape codes, working in any environment from modern terminals to legacy systems.

```bash
./pit image.jpg
```
Features
 * True color rendering with automatic fallback to 256/16-color modes.
 * Advanced image manipulation via CLI: zoom, pan, flip, and rotate.
 * Customizable background color for transparent PNG images.
 * Automatic optimal sizing to fit terminal, preserving aspect ratio.
 * Zero dependencies beyond standard libraries.
 * Cross-platform support for Linux, macOS, Windows, and embedded systems.
 * Supports multiple architectures: x86, ARM, RISC-V, PowerPC, MIPS.
Installation


### 🖼️ Demo

<p align="center">
  <img src="https://github.com/Ferki-git-creator/phono-in-terminal-image-viewer-rgb-c-textmode/blob/main/demo/new(best)/test.png?raw=true" alt="Demo Image" width="400"/>
  <br/>
  <img src="https://github.com/Ferki-git-creator/phono-in-terminal-image-viewer-rgb-c-textmode/blob/main/demo/new(best)/linux_tux.png?raw=true" alt="Linux Tux Demo" width="400"/>
</p>

---

### 🎥 Video Preview

📺 [Watch on YouTube](https://youtube.com/shorts/TcBFNJcPX-U?si=BA3sVxqZCKaehBu2)

# Install

```bash
# Clone repository
git clone https://github.com/Ferki-git-creator/phono-in-terminal-image-viewer-rgb-c-textmode
cd pit
```
# Build the executable
```bash
chmod +x build.sh
./build.sh
# The executable 'pit' will be created in the current directory.
# You can optionally move it to a directory in your PATH, e.g.:
# sudo mv pit /usr/local/bin
```
Usage
Basic syntax:
```
pit [options] <image-file>
```
Options:
```
 * --width, -w <columns>: Set output width in terminal columns. Overrides auto-sizing.
 * --height, -H <rows>: Set output height in terminal rows. Overrides auto-sizing.
 * --zoom <factor>: Zoom level. 1.0 is default (fit to terminal). 2.0 zooms in (shows a smaller portion of the image, appearing larger). 0.5 zooms out (shows a larger portion, appearing smaller).
 * --offset-x <pixels>: Horizontal offset (pan right) in original image pixels.
 * --offset-y <pixels>: Vertical offset (pan down) in original image pixels.
 * --flip-h: Flip image horizontally.
 * --flip-v: Flip image vertically.
 * --rotate <degrees>: Rotate image clockwise (supports 90, 180, 270 degrees).
 * --bg <color>: Background color for PNG transparency (e.g., 'black', 'white'). Default is black.
```
Examples:
```bash
# Display image with automatic optimal sizing
pit photo.jpg

# Specify output width
pit image.png --width 80

# Zoom in on a portion of the image
pit landscape.jpg --zoom 2.0

# Pan right and down
pit character.png --offset-x 50 --offset-y 20

# Flip horizontally and rotate 90 degrees clockwise
pit diagram.jpg --flip-h --rotate 90

# Display transparent PNG with a white background
pit logo.png --bg white
```

Compatibility
| Platform | Status | Notes |
|---|---|---|
| Linux (TTY) | ✅ Working | Best experience |
| macOS | ✅ Working | Tested with Terminal.app |
| Windows | ✅ Working | Windows Terminal |
| Termux (Android) | ✅ Working | ARM builds supported, optimal sizing adjusted |
| WSL | ✅ Working | Both WSL1 and WSL2 |
| BSD Systems | ✅ Working | Tested on FreeBSD |

Terminal Requirements:
 * Supports at least 16 colors
 * Minimum 80x25 character resolution
Terminal Character Aspect Ratio:
The program attempts to automatically adjust for the common terminal character aspect ratio (characters being taller than wide, typically 1:2). If images appear stretched or squashed, you might need to adjust the #define TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO value in pit.c to match your specific terminal's font.
Building from Source
 * Ensure you have a C compiler installed (gcc, clang, or tcc).
 * Run the build script:
   ```bash
   ./build.sh
   ```
   The script will prompt you to choose between Debug (for development and logging) and Release (for optimized performance and smaller size) build types.
For advanced builds:
```bash
# Build for a specific architecture (e.g., aarch64 for ARM64)
./build.sh build aarch64

# Build a static executable (useful for distribution without dependencies)
./build.sh build x86_64 # (then choose 'y' for static build)
```
Contributing
Contributions are welcome! Please follow these steps:
 * Fork the repository
 * Create a feature branch (git checkout -b feature/your-feature)
 * Commit your changes (git commit -am 'Add some feature')
 * Push to the branch (git push origin feature/your-feature)
 * Open a pull request

📦 Portable: Single binary (around 250KB for ARM64)

⚡ Fast: Renders efficiently on various systems

🌍 Universal: Runs on any architecture with a C compiler
Connection

## ☕ Support

I would be happy for a coffee if you enjoy my work!

<a href="https://ko-fi.com/ferki" target="_blank">
  <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Buy Me a Coffee at ko-fi.com" />
</a>

If you have any questions, you can contact me: denisdola278@gmail.com .

If you want to support, just put a star on the repository.😀

