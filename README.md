# 🎨 PIT - Phono in Terminal

**PIT (Phono in Terminal)** is a lightweight, cross-platform terminal image viewer. It renders PNG and JPEG images using ANSI escape codes, working on both modern terminals and legacy TTY environments.

## 🚀 Quick Start

```bash
# Clone the repository
git clone https://github.com/Ferki-git-creator/phono-in-terminal-image-viewer-rgb-c-textmode
cd pit

# Build the executable
chmod +x build.sh
./build.sh


# (Optional) Move it to a directory in your PATH

sudo mv pit /usr/local/bin
```

* 🖼️ Features

* ✅ True color rendering with fallback to 256/16-color modes

* 🌀 Image manipulation via CLI: zoom, pan, flip, rotate

* 🎨 Custom background color for transparent PNGs

* 📐 Auto resize to terminal while preserving aspect ratio

* 📦 Zero dependencies beyond standard libraries

* ⚙️ Cross-platform: Linux, macOS, Windows, Termux, WSL, BSD

* 💻 Supports x86, ARM, RISC-V, MIPS, PowerPC


📚 Usage
```bash
pit [options] <image-file>

⚙️ Options

Option	Description

--width, -w <cols>	Set output width in terminal columns
--height, -H <rows>	Set output height in terminal rows
--zoom <factor>	Zoom level (1.0 = fit, 2.0 = zoom in, 0.5 = zoom out)
--offset-x <px>	Horizontal pan offset (in image pixels)
--offset-y <px>	Vertical pan offset
--flip-h	Flip image horizontally
--flip-v	Flip image vertically
--rotate <deg>	Rotate image clockwise (90, 180, 270)
--bg <color>	Background color for transparency (e.g. black, white)
```

🔍 Examples
```bash
pit photo.jpg                            # Auto-fit
pit image.png --width 80                 # Set width
pit landscape.jpg --zoom 2.0             # Zoom in
pit character.png --offset-x 50 --offset-y 20
pit diagram.jpg --flip-h --rotate 90
pit logo.png --bg white                  # Set background for transparent PNG
```
🧪 Compatibility
| Platform | Status | Notes |
|---|---|---|
| Linux (TTY) | ✅ Working | Best experience |
| macOS | ✅ Working | Tested with Terminal.app |
| Windows | ✅ Working | Windows Terminal |
| Termux (Android) | ✅ Working | ARM builds supported, optimal sizing adjusted |
| WSL | ✅ Working | Both WSL1 and WSL2 |
| BSD Systems | ✅ Working | Tested on FreeBSD |

Terminal Requirements:

* At least 16 color support

* Minimum 80x25 resolution


Aspect Ratio Note:
```text
Terminal characters are usually taller than wide (approx. 1:2).
If images look distorted, adjust the #define TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO in pit.c.
```
🛠️ Building from Source

* Requires a C compiler (gcc, clang, or tcc)

* Run:

```bash
./build.sh
```
> The script lets you choose between:

> Debug build (with logging)

> Release build (optimized, smaller binary)


🔧 Advanced Builds
```bash
./build.sh build aarch64     # Build for ARM64
./build.sh build x86_64      # Build for x86_64 (supports static linking)
```
🙌 Contributing

1. Fork the repo


2. Create a branch: git checkout -b feature/your-feature


3. Commit your changes: git commit -am 'Add some feature'


4. Push the branch: git push origin feature/your-feature


5. Open a pull request



🌟 Highlights

📦 Portable: Single binary (~150 KB for ARM64)

⚡ Fast: Runs even on low-end machines

🌍 Universal: Works anywhere a C compiler is available


📫 Contact

If you have questions, reach me at:
📧 denisdola278@gmail.com



