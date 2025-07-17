#!/bin/bash

# --- Configuration ---
COMPILER_PREFERENCE=("gcc" "clang" "tcc") # Preferred compilers in order
DEFAULT_COMPILER="gcc" # Fallback if none in preference are found
BUILD_DIR="build"
BIN_NAME="" # Will be set based on user choice
SRC_FILE="" # Will be set based on user choice
STB_IMAGE_H="stb_image.h"
STB_IMAGE_URL="https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
LOG_FILE="build.log" # Define log file name

# Define colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Logging Functions ---
# These functions now output to both stdout/stderr and build.log
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# --- Help Message ---
print_help() {
    echo "Usage: ./build.sh [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  build [architecture]  Builds the specified 'pit' or 'pit_gif' executable."
    echo "                        <architecture> can be 'x86_64', 'aarch64', 'armv7l', 'riscv64', 'ppc64le', 'mips', 'i386'."
    echo "                        If no architecture is specified, it detects the host architecture."
    echo "  clean                 Removes the build directory and executables."
    echo "  cross <compiler> <architecture>"
    echo "                        Performs cross-compilation. E.g., 'cross aarch64-linux-gnu-gcc aarch64'."
    echo ""
    echo "Options:"
    echo "  -h, --help            Show this help message and exit."
}

# --- Handle help command early ---
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  print_help
  exit 0
fi

# --- Log Output Choice ---
LOG_OUTPUT_CHOICE=""
while [[ "$LOG_OUTPUT_CHOICE" != "f" && "$LOG_OUTPUT_CHOICE" != "t" ]]; do
    read -p "$(echo -e "${BLUE}Where to output build log? (f=file, t=terminal, default: f): ${NC}")" LOG_OUTPUT_INPUT
    LOG_OUTPUT_CHOICE=${LOG_OUTPUT_INPUT:-f} # Default to file
    LOG_OUTPUT_CHOICE=$(echo "$LOG_OUTPUT_CHOICE" | tr '[:upper:]' '[:lower:]')
    if [[ "$LOG_OUTPUT_CHOICE" != "f" && "$LOG_OUTPUT_CHOICE" != "t" ]]; then
        log_warning "Invalid choice. Please enter 'f' or 't'."
    fi
done

# --- Redirect all output based on user choice ---
if [ "$LOG_OUTPUT_CHOICE" == "f" ]; then
    if command -v tee &> /dev/null; then
        exec > >(tee "${LOG_FILE}") 2>&1
        log_info "All build output redirected to ${LOG_FILE}"
    else
        log_warning "tee command not found. Output will not be logged to ${LOG_FILE}."
    fi
else # LOG_OUTPUT_CHOICE == "t"
    log_info "Build output will be shown in terminal only."
    # No redirection needed, it's already stdout/stderr
fi


# --- Check Prerequisites ---
detect_compiler() {
    log_info "Detecting suitable C compiler..."
    for compiler in "${COMPILER_PREFERENCE[@]}"; do
        if command -v "$compiler" &> /dev/null; then
            COMPILER="$compiler"
            log_success "Using compiler: ${COMPILER}"
            return 0
        fi
    C_COMPILER_FOUND=0
    done
    COMPILER="${DEFAULT_COMPILER}" # Fallback
    log_warning "None of the preferred compilers (${COMPILER_PREFERENCE[*]}) found. Falling back to default: ${COMPILER}"
    if ! command -v "${COMPILER}" &> /dev/null; then
        log_error "${COMPILER} also not found. Please install a C compiler (e.g., gcc, clang)."
        exit 1
    fi
    return 0
}

download_stb_image_h() {
    if [ ! -f "${STB_IMAGE_H}" ]; then
        log_info "stb_image.h not found. Attempting to download from ${STB_IMAGE_URL}..."
        if command -v curl &> /dev/null; then
            curl -s -O "${STB_IMAGE_URL}"
            if [ $? -eq 0 ]; then
                log_success "Successfully downloaded ${STB_IMAGE_H}"
            else
                log_error "Failed to download ${STB_IMAGE_H}. Check your internet connection or URL."
                exit 1
            fi
        elif command -v wget &> /dev/null; then
            wget -q "${STB_IMAGE_URL}"
            if [ $? -eq 0 ]; then
                log_success "Successfully downloaded ${STB_IMAGE_H}"
            else
                log_error "Failed to download ${STB_IMAGE_H}. Check your internet connection or URL."
                exit 1
            fi
        else
            log_error "Neither curl nor wget found. Cannot download ${STB_IMAGE_H}. Please download it manually or install curl/wget."
            exit 1
        fi
    else
        log_info "${STB_IMAGE_H} already exists."
    fi
}

# --- Clean Function ---
clean() {
    log_info "Cleaning build directory and executables..."
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        if [ $? -ne 0 ]; then
            log_warning "Failed to remove build directory: ${BUILD_DIR}"
        fi
    fi
    
    if [ -f "${LOG_FILE}" ]; then # Also clean the log file
        rm "${LOG_FILE}"
        if [ $? -ne 0 ]; then
            log_warning "Failed to remove log file: ${LOG_FILE}"
        fi
    fi
    # Check for both pit and pit_gif executables in the current directory if they were moved there
    if [ -f "pit" ]; then
        rm "pit"
        log_info "Removed old 'pit' executable from root."
    fi
    if [ -f "pit_gif" ]; then
        rm "pit_gif"
        log_info "Removed old 'pit_gif' executable from root."
    fi

    if [ ! -d "${BUILD_DIR}" ] && [ ! -f "${LOG_FILE}" ] && [ ! -f "pit" ] && [ ! -f "pit_gif" ]; then
        log_success "Clean complete."
    else
        log_warning "Clean finished, but some files/directories might remain due to permissions or other issues."
    fi
}

# --- Build Function ---
build() {
    log_info "Starting build process..."

    detect_compiler
    download_stb_image_h

    # Create build directory if it doesn't exist
    mkdir -p "${BUILD_DIR}"
    if [ $? -ne 0 ]; then
        log_error "Failed to create build directory: ${BUILD_DIR}"
        exit 1
    fi

    # --- Build Target Choice ---
    BUILD_TARGET_CHOICE=""
    while [[ "$BUILD_TARGET_CHOICE" != "p" && "$BUILD_TARGET_CHOICE" != "g" ]]; do
        read -p "$(echo -e "${BLUE}Which executable to build? (p=pit, g=pit_gif, default: p): ${NC}")" BUILD_TARGET_INPUT
        BUILD_TARGET_CHOICE=${BUILD_TARGET_INPUT:-p} # Default to pit
        BUILD_TARGET_CHOICE=$(echo "$BUILD_TARGET_CHOICE" | tr '[:upper:]' '[:lower:]')
        if [[ "$BUILD_TARGET_CHOICE" != "p" && "$BUILD_TARGET_CHOICE" != "g" ]]; then
            log_warning "Invalid choice. Please enter 'p' or 'g'."
        fi
    done

    if [ "$BUILD_TARGET_CHOICE" == "p" ]; then
        SRC_FILE="./pit.c"
        BIN_NAME="pit"
        log_info "Building 'pit' (static image viewer)."
    else # BUILD_TARGET_CHOICE == "g"
        SRC_FILE="./pit_gif.c"
        BIN_NAME="pit_gif"
        log_info "Building 'pit_gif' (GIF 'animation' viewer)."
    fi


    # Determine target architecture
    local TARGET_ARCH
    if [ -z "$1" ]; then
        log_info "Detecting host architecture..."
        TARGET_ARCH=$(uname -m | tr '[:upper:]' '[:lower:]')
        log_info "Detected host architecture: ${TARGET_ARCH}"
    else
        TARGET_ARCH="$1"
        log_info "Building for specified architecture: ${TARGET_ARCH}"
    fi

    local CFLAGS="-Wall -Wextra -pedantic -std=c11 -funroll-loops" # Added -funroll-loops
    local LDFLAGS="-lm" 
    local TARGET_SPEC=""
    local STATIC_BUILD_FLAG="no"
    local SANITIZER_FLAGS=""
    local BUILD_TYPE # Declare BUILD_TYPE here

    # --- CI/CD Automation ---
    # If CI environment variable is set, force Release build with static linking
    if [ "$CI" = "true" ]; then
        BUILD_TYPE="Release"
        STATIC_BUILD_FLAG="yes"
        SANITIZER_FLAGS="" # Ensure no sanitizers in CI
        log_info "CI mode detected: Forcing Release build with static linking."
    else
        # Determine build type (debug or release) interactively with default
        read -p "$(echo -e "${BLUE}Choose build type (d/r for Debug/Release, default: r): ${NC}")" BUILD_TYPE_INPUT
        BUILD_TYPE_INPUT=${BUILD_TYPE_INPUT:-r} # Default to release
        BUILD_TYPE_INPUT=$(echo "$BUILD_TYPE_INPUT" | tr '[:upper:]' '[:lower:]') # Convert to lowercase
        
        if [ "$BUILD_TYPE_INPUT" == "d" ]; then
            BUILD_TYPE="Debug"
            CFLAGS+=" -g" # Add debug symbols
            log_info "Building in Debug mode."

            # Ask for sanitizers with default
            read -p "$(echo -e "${BLUE}Enable sanitizers? (a=address, u=undefined, t=thread, default: none): ${NC}")" SANITIZERS_INPUT
            SANITIZERS_INPUT=${SANITIZERS_INPUT:-none} # Default to none
            SANITIZERS_INPUT=$(echo "$SANITIZERS_INPUT" | tr '[:upper:]' '[:lower:]') # Convert to lowercase

            if [[ "$SANITIZERS_INPUT" == *a* ]]; then SANITIZER_FLAGS+=" -fsanitize=address"; fi
            if [[ "$SANITIZERS_INPUT" == *u* ]]; then SANITIZER_FLAGS+=" -fsanitize=undefined"; fi
            if [[ "$SANITIZERS_INPUT" == *t* ]]; then SANITIZER_FLAGS+=" -fsanitize=thread"; fi
            
            if [ -n "$SANITIZER_FLAGS" ]; then
                CFLAGS+=" ${SANITIZER_FLAGS}"
                LDFLAGS+=" ${SANITIZER_FLAGS}" # Sanitizers also need to be linked
                log_info "Sanitizers enabled: ${SANITIZER_FLAGS}"
                # Removed -flto if sanitizers are enabled - now -flto is not added by default for release
            fi

        else
            BUILD_TYPE="Release"
            CFLAGS+=" -O3" # Optimization level 3 (removed -flto)
            log_info "Building in Release mode (without LTO). If you need LTO, add -flto manually."
        fi

        # Ask for static build interactively with default
        read -p "$(echo -e "${BLUE}Enable static build? (y/N, default: N): ${NC}")" STATIC_BUILD_INPUT
        STATIC_BUILD_INPUT=${STATIC_BUILD_INPUT:-n}
        STATIC_BUILD_INPUT=$(echo "$STATIC_BUILD_INPUT" | tr '[:upper:]' '[:lower:]')
        if [ "$STATIC_BUILD_INPUT" == "y" ]; then
            STATIC_BUILD_FLAG="yes"
            LDFLAGS+=" -static"
            log_info "Static build enabled."
        fi
    fi # End of CI/interactive build type selection

    log_info "Initial compiler flags: ${CFLAGS}"

    # Special handling for different architectures or cross-compilation
    case "${TARGET_ARCH}" in
        "x86_64"|"amd64")
            log_info "Target is x86_64."
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -march=native -mtune=native" # Optimize for host CPU architecture
            fi
            # Windows specific flags for x86_64 (detected via uname -s)
            if [[ "$(uname -s)" == *"MINGW"* || "$(uname -s)" == *"MSYS"* || "$(uname -s)" == *"CYGWIN"* ]]; then
                log_info "Detected Windows environment (MSYS/MinGW/Cygwin)."
                CFLAGS+=" -D_WIN32_WINNT=0x0A00"  # Target Windows 10+ API
                LDFLAGS+=" -luser32 -lkernel32"
                if [ "$STATIC_BUILD_FLAG" == "yes" ]; then
                    LDFLAGS+=" -static-libgcc -static-libstdc++" # Ensure static linking of runtime libs
                fi
            fi
            ;;
        "aarch64"|"arm64")
            log_info "Target is AArch64 (ARM64)."
            if [ "$BUILD_TYPE" == "Release" ]; then
                # Optimizations for ARMv8 with SIMD (NEON), CRC, Crypto, tuned for Cortex-A73
                CFLAGS+=" -march=armv8-a+simd+crc+crypto -mtune=cortex-a73"
                CFLAGS+=" -fomit-frame-pointer -flax-vector-conversions" # Added specific optimizations
            fi
            # Specific optimization for Termux (Android environment)
            if [[ "$OSTYPE" == *"android"* ]]; then
                CFLAGS+=" -DTERMUX_ENV"
                log_info "Termux environment detected. Adding -DTERMUX_ENV."
            fi
            # Check for specific AArch64 cross-compiler
            if [ "$CROSS_COMPILE" == "1" ]; then # If cross-compile explicitly requested
                if command -v aarch64-linux-gnu-gcc &> /dev/null; then
                    COMPILER="aarch64-linux-gnu-gcc"
                    log_info "Using AArch64 cross-compiler: ${COMPILER}"
                elif command -v arm-linux-gnueabihf-gcc &> /dev/null; then # Fallback for older arm64/armhf
                    COMPILER="arm-linux-gnueabihf-gcc"
                    log_info "Using ARM cross-compiler: ${COMPILER}"
                else
                    log_warning "No specific AArch64 cross-compiler found. Attempting to use host compiler. This may fail if libraries are not compatible."
                fi
            fi
            ;;
        "armv7l"|"arm")
            log_info "Target is ARMv7."
            if [ "$BUILD_TYPE" == "Release" ]; then
                # Optimize for ARMv7 with NEON and VFPv4 (common for mobile/embedded)
                CFLAGS+=" -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
                CFLAGS+=" -fomit-frame-pointer -flax-vector-conversions" # Added specific optimizations
            fi
            if [ "$CROSS_COMPILE" == "1" ]; then
                if command -v arm-linux-gnueabihf-gcc &> /dev/null; then
                    COMPILER="arm-linux-gnueabihf-gcc"
                    log_info "Using ARMv7 cross-compiler: ${COMPILER}"
                else
                    log_warning "No specific ARMv7 cross-compiler found. Attempting to use host compiler."
                fi
            fi
            ;;
        "riscv64")
            log_info "Target is RISC-V 64."
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -march=rv64gc -mabi=lp64d" # Standard RISC-V 64-bit general-purpose with double-precision float
                CFLAGS+=" -fomit-frame-pointer"
            fi
            if [ "$CROSS_COMPILE" == "1" ]; then
                if command -v riscv64-linux-gnu-gcc &> /dev/null; then
                    COMPILER="riscv64-linux-gnu-gcc"
                    log_info "Using RISC-V 64 cross-compiler: ${COMPILER}"
                else
                    log_warning "No specific RISC-V 64 cross-compiler found. Attempting to use host compiler."
                fi
            fi
            ;;
        "ppc64le")
            log_info "Target is PowerPC64 LE."
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -mcpu=power8" # Example CPU for PowerPC
                CFLAGS+=" -fomit-frame-pointer"
            fi
            if [ "$CROSS_COMPILE" == "1" ]; then
                if command -v powerpc64le-linux-gnu-gcc &> /dev/null; then
                    COMPILER="powerpc64le-linux-gnu-gcc"
                    log_info "Using PowerPC64 LE cross-compiler: ${COMPILER}"
                else
                    log_warning "No specific PowerPC64 LE cross-compiler. Attempting to use host compiler."
                F_COMPILER_FOUND=0
                fi
            fi
            ;;
        "mips"|"mipsel"|"mips64")
            log_info "Target is MIPS."
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -fomit-frame-pointer"
            fi
            if [ "$CROSS_COMPILE" == "1" ]; then
                if command -v mips-linux-gnu-gcc &> /dev/null; then
                    COMPILER="mips-linux-gnu-gcc"
                    log_info "Using MIPS cross-compiler: ${COMPILER}"
                else
                    log_warning "No specific MIPS cross-compiler found. Attempting to use host compiler."
                fi
            fi
            ;;
        "i386"|"i686")
            log_info "Target is i386/i686 (32-bit Intel)."
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -march=i686 -mtune=generic" # Optimize for i686
            fi
            ;;
        *)
            log_warning "Unknown or unsupported target architecture: ${TARGET_ARCH}. Building for host."
            # Attempt native optimization for unknown targets if release build
            if [ "$BUILD_TYPE" == "Release" ]; then
                CFLAGS+=" -march=native"
            fi
            ;;
    esac
    
    # Add -mfloat-abi=hard for ARM targets that typically use it, if not already specified by march
    if [[ "$TARGET_ARCH" == arm* ]] && ! [[ "$CFLAGS" == *"-mfloat-abi=hard"* ]]; then
        CFLAGS+=" -mfloat-abi=hard"
    fi

    log_info "Final compiler flags: ${CFLAGS}"
    log_info "Final linker flags: ${LDFLAGS}"

    # --- Two-step build process: Compile to .o, then link .o to executable ---

    # Step 1: Compile source file to object file
    log_info "Compiling ${SRC_FILE} to object file: ${BUILD_DIR}/${BIN_NAME}.o"
    "${COMPILER}" ${CFLAGS} ${TARGET_SPEC} -c "${SRC_FILE}" -o "${BUILD_DIR}/${BIN_NAME}.o"

    if [ $? -ne 0 ]; then
        log_error "Compilation to object file failed!"
        exit 1
    fi

    # Step 2: Link object file to create the executable
    log_info "Linking object file ${BUILD_DIR}/${BIN_NAME}.o to create executable: ${BUILD_DIR}/${BIN_NAME}"
    "${COMPILER}" "${BUILD_DIR}/${BIN_NAME}.o" -o "${BUILD_DIR}/${BIN_NAME}" ${LDFLAGS}

    if [ $? -eq 0 ]; then
        log_success "Compilation and Linking successful!"
        log_info "Executable created at: ${BUILD_DIR}/${BIN_NAME}"
        log_info "To run: ./${BUILD_DIR}/${BIN_NAME} <image-file>"
        log_info "Example: ./${BUILD_DIR}/${BIN_NAME} assets/test.jpg"
    else
        log_error "Build failed!"
        exit 1
    fi
}

# --- Main Script Logic ---
case "$1" in
    "clean")
        clean
        ;;
    "build")
        # Shift to get the architecture argument if provided
        shift
        build "$@"
        ;;
    "cross")
        # Shift to get the compiler argument (e.g., aarch64-linux-gnu-gcc)
        shift
        export CC="$1" # Set CC environment variable for cross-compiler
        export CROSS_COMPILE=1 # Indicate cross-compilation mode
        shift # Shift again to get architecture (e.g., aarch64)
        build "$@"
        ;;
    *)
        log_info "No command provided. Defaulting to 'build' (run with './build.sh clean' to remove built files)."
        log_info "Usage: ./build.sh [build <architecture>] | cross <compiler> <architecture> | clean"
        log_info "  <architecture> can be 'x86_64', 'aarch64', 'armv7l', 'riscv64', 'ppc64le', 'mips', 'i386', or empty for host detection."
        log_info "  For cross-compilation: ./build.sh cross aarch64-linux-gnu-gcc aarch64"
        build # Default action
        ;;
esac

exit 0
