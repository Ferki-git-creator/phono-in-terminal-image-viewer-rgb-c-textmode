/**
 * @file pit.c
 * @brief PIT - Phono in Terminal. A command-line image viewer for rendering images in the terminal.
 *
 * This program loads an image using stb_image, resizes it using bilinear interpolation,
 * and renders it in the terminal using ANSI escape codes for 24-bit color.
 * It now supports command-line arguments for zooming, panning, flipping, rotating,
 * and setting a background color for transparent images.
 */

#define _POSIX_C_SOURCE 200809L // For popen and pclose declarations

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // For usleep (though not used in final non-interactive version)
#include <errno.h>
#include <ctype.h>   // For isdigit
#include <signal.h>  // Correct include for signal handling
#include <stdint.h>  // For uint64_t
#include <stdbool.h> // For bool type
#include <math.h>    // For pow (used by stb_image for HDR, linked with -lm)

// Include for SIMD intrinsics (placeholders for future vectorization)
#ifdef __SSE2__
#include <emmintrin.h> // For SSE2 intrinsics (x86)
#endif

#ifdef __ARM_NEON
#include <arm_neon.h> // For ARM NEON intrinsics
#endif

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif

// STB Image defines for specific features/formats
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// --- Global Variables ---
/**
 * @brief Original width of the loaded image.
 */
static int s_original_width;
/**
 * @brief Original height of the loaded image.
 */
static int s_original_height;
/**
 * @brief Original number of channels (e.g., 3 for RGB, 4 for RGBA).
 */
static int s_original_channels;
/**
 * @brief Pointer to the raw pixel data of the original loaded image.
 * This memory is managed by stb_image.
 */
static unsigned char *s_original_image_data = NULL;

// Cached terminal dimensions to avoid repeated system calls
static int s_term_width = 0;
static int s_term_height = 0;

/**
 * @brief Enum for detected terminal color modes.
 */
typedef enum {
    COLOR_MODE_UNKNOWN = 0,
    COLOR_MODE_16,
    COLOR_MODE_256,
    COLOR_MODE_TRUE_COLOR
} ColorMode;

/**
 * @brief Global variable for the detected terminal color mode.
 */
static ColorMode s_detected_color_mode = COLOR_MODE_UNKNOWN;

// Image cache is no longer strictly needed for single render, but kept for future expansion
/**
 * @brief Structure to cache resized image data.
 */
typedef struct {
    int width;
    int height;
    unsigned char* data;
} ImageCacheEntry;

/**
 * @brief Dynamic array to store cached image entries.
 */
static ImageCacheEntry* s_image_cache = NULL;
/**
 * @brief Current number of entries in the image cache.
 */
static int s_cache_size = 0;

/**
 * @brief Cache for 16-color ANSI escape codes.
 */
static char* s_ansi_cache_16[16] = {NULL};
/**
 * @brief Cache for 256-color ANSI escape codes.
 */
static char* s_ansi_cache_256[256] = {NULL};


// --- Logging Macros ---
/**
 * @brief Custom error logging macro to include file and line number.
 * @param ... Variable arguments for the format string.
 */
#define LOG_ERROR(...) do { \
    fprintf(stderr, "[ERROR] %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

/**
 * @brief Custom warning logging macro to include file and line number.
 * @param ... Variable arguments for the format string.
 */
#define LOG_WARNING(...) do { \
    fprintf(stderr, "[WARNING] %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

/**
 * @brief Custom info logging macro to include file and line number.
 * @param ... Variable arguments for the format string.
 */
#define LOG_INFO(...) do { \
    fprintf(stderr, "[INFO] %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)


// --- Utility Macro for min ---
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

// --- Configurable Terminal Character Aspect Ratio ---
// This value represents (Character Height / Character Width).
// Common values:
//   2.0f: Characters are 2 times taller than wide (e.g., 1x2 pixel ratio). Typical for many older terminals.
//   1.0f: Characters are square (1x1 pixel ratio). Common in some modern terminals/fonts.
//   1.5f: Characters are 1.5 times taller than wide.
// Adjust this value if images appear stretched or squashed in your terminal.
// For Termux, if a square image is "squashed horizontally" (appears taller and narrower),
// it means the terminal characters are taller than assumed.
// Increasing this value will make the image appear wider to compensate.
#define TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO 1.5f 

// --- Function Prototypes ---
void print_help(void);
void get_terminal_size(int *width, int *height);
void detect_color_support(void);
static int rgb_to_256(unsigned char r, unsigned char g, unsigned char b);
static int rgb_to_16(unsigned char r, unsigned char g, unsigned char b);
static int format_ansi_color_code(char* buf, unsigned char r, unsigned char g, unsigned char b, ColorMode mode);
void render_image(unsigned char *img_data, int width, int height, int channels, unsigned char bg_r, unsigned char bg_g, unsigned char bg_b);
unsigned char* resize_image_bilinear(unsigned char *img_data, int orig_w, int orig_h, int orig_channels,
                                     int src_x, int src_y, int src_w, int src_h, // Source rectangle in original image
                                     int new_w, int new_h); // Destination dimensions
void calculate_display_dimensions(int img_orig_width, int img_orig_height, float zoom_factor,
                                  int *display_width, int *display_height);
// Image transformation prototypes
unsigned char* flip_image_horizontal(unsigned char *img_data, int w, int h, int c);
unsigned char* flip_image_vertical(unsigned char *img_data, int w, int h, int c);
unsigned char* rotate_image_90_cw(unsigned char *img_data, int *w, int *h, int c); // w, h are pointers as they swap
unsigned char* rotate_image_180(unsigned char *img_data, int w, int h, int c);


// Removed raw mode functions: enable_raw_mode, disable_raw_mode
// Removed handle_signal as it's not needed without raw mode and atexit
unsigned char* get_cached_image(int width, int height); // Kept for future expansion
void add_to_cache(int width, int height, unsigned char* data); // Kept for future expansion
void free_image_cache(void); // Prototype for the function below
void init_ansi_cache(void);
void free_ansi_cache(void);


/**
 * @brief Prints the help message to stdout.
 * Provides usage instructions, available options, and interactive controls.
 */
void print_help(void) {
    printf("PIT - Phono in Terminal\n");
    printf("Usage: pit [options] <image-file>\n\n");
    printf("Options:\n");
    printf("  --width, -w <columns>  Set output width (columns). Overrides auto-sizing.\n");
    printf("  --height, -H <rows>    Set output height (rows). Overrides auto-sizing.\n");
    printf("  --zoom <factor>        Zoom level. `1.0` is default (fit to terminal). `2.0` zooms in (shows a smaller portion of the image, appearing larger). `0.5` zooms out (shows a larger portion, appearing smaller).\n");
    printf("  --offset-x <pixels>    Horizontal offset (pan right) in original image pixels.\n");
    printf("  --offset-y <pixels>    Vertical offset (pan down) in original image pixels.\n");
    printf("  --flip-h               Flip image horizontally.\n");
    printf("  --flip-v               Flip image vertically.\n");
    printf("  --rotate <degrees>     Rotate image (90, 180, 270 degrees clockwise).\n");
    printf("  --bg <color>           Background color for PNG transparency (e.g., 'black', 'white'). Default: black.\n");
    printf("  --help                 Show this help\n");
    printf("  --version              Show version\n\n");
    
    printf("\nTerminal Character Aspect Ratio:\n");
    printf("  Current assumed ratio (Height/Width): %.2f. Adjust #define TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO in pit.c if images appear stretched/squashed.\n", TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);

    printf("\nColor support detection:\n");
    printf("  Detected terminal color mode: ");
    // Ensure color mode is detected before printing help
    if (s_detected_color_mode == COLOR_MODE_UNKNOWN) {
        detect_color_support();
    }
    switch (s_detected_color_mode) {
        case COLOR_MODE_TRUE_COLOR: printf("24-bit true color"); break;
        case COLOR_MODE_256: printf("256 colors"); break;
        case COLOR_MODE_16: printf("16 colors"); break;
        default: printf("unknown"); break;
    }
    printf("\n");
    
    printf("\nCompatibility:\n");
    printf("  Supported architectures: x86_64, ARM, RISC-V, PowerPC, MIPS\n");
    printf("  Supported terminals: Linux console, macOS Terminal, iTerm2,\n");
    printf("                       Windows Terminal, Termux, xterm, and more\n");
}

/**
 * @brief Attempts to get the terminal size using various methods.
 * Caches the results to avoid repeated system calls.
 * @param width Pointer to store the terminal width in columns.
 * @param height Pointer to store the terminal height in rows.
 */
void get_terminal_size(int *width, int *height) {
    // Return cached values if available
    if(s_term_width > 0 && s_term_height > 0) {
        *width = s_term_width;
        *height = s_term_height;
        return;
    }

    int detected_width = 0;
    int detected_height = 0;

#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        detected_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        detected_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    // 1. Attempt via ioctl (Linux/macOS)
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        detected_width = w.ws_col;
        detected_height = w.ws_row;
    }
    
    // 2. Attempt via environment variables if ioctl failed or returned invalid sizes
    if (detected_width <= 0 || detected_height <= 0) {
        char *cols_str = getenv("COLUMNS");
        char *rows_str = getenv("LINES");
        if (cols_str && rows_str) {
            int c = atoi(cols_str);
            int r = atoi(rows_str);
            if (c > 0 && r > 0) {
                detected_width = c;
                detected_height = r;
            }
        }
    }
    
    // 3. Fallback: using tput (requires ncurses-base or similar)
    if (detected_width <= 0 || detected_height <= 0) {
        FILE *fp_cols = popen("tput cols 2>/dev/null", "r");
        if (fp_cols) {
            if (fscanf(fp_cols, "%d", &detected_width) != 1) detected_width = 0;
            pclose(fp_cols);
        }
        
        FILE *fp_lines = popen("tput lines 2>/dev/null", "r");
        if (fp_lines) {
            if (fscanf(fp_lines, "%d", &detected_height) != 1) detected_height = 0;
            pclose(fp_lines);
        }
    }
#endif

    // Apply fallback defaults if all attempts fail
    if (detected_width <= 0) detected_width = 80;
    if (detected_height <= 0) detected_height = 24;

    // Cache the detected dimensions
    s_term_width = detected_width;
    s_term_height = detected_height;

    *width = s_term_width;
    *height = s_term_height;

    LOG_INFO("Detected terminal size: %dx%d", *width, *height);
}

/**
 * @brief Detects the terminal's color support capabilities.
 * Sets the global `s_detected_color_mode` variable.
 */
void detect_color_support(void) {
#ifdef _WIN32
    // Windows 10+ supports 24-bit true colors in modern terminals (Windows Terminal, VS Code)
    // For older consoles, it might be limited, but assuming modern usage.
    s_detected_color_mode = COLOR_MODE_TRUE_COLOR;
    return;
#else
    char* colorterm = getenv("COLORTERM");
    char* term = getenv("TERM");
    
    if (colorterm && (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit"))) {
        s_detected_color_mode = COLOR_MODE_TRUE_COLOR;
        return;
    }
    
    // Special check for terminals that might not set COLORTERM but support true color
    if (getenv("KONSOLE_PROFILE_NAME") || getenv("KONSOLE_VERSION")) {
        s_detected_color_mode = COLOR_MODE_TRUE_COLOR;
        return;
    }
    if (getenv("TERM_PROGRAM") && strstr(getenv("TERM_PROGRAM"), "iTerm")) {
        s_detected_color_mode = COLOR_MODE_TRUE_COLOR;
        return;
    }
    
    if (term) {
        // Check for common 256-color terminals
        const char* terms_256[] = {
            "xterm-256color", "screen-256color", "tmux-256color",
            "rxvt-unicode-256color", "linux-16color", "eterm-256color"
        };
        
        for (size_t i = 0; i < sizeof(terms_256)/sizeof(terms_256[0]); i++) {
            if (strstr(term, terms_256[i])) {
                s_detected_color_mode = COLOR_MODE_256;
                return;
            }
        }
        
        // Check for basic 16-color support
        if (strstr(term, "xterm") || strstr(term, "screen") || 
            strstr(term, "vt100") || strstr(term, "ansi") ||
            strstr(term, "linux")) { // Linux console is often 16-color
            s_detected_color_mode = COLOR_MODE_16;
            return;
        }
    }
    
    // Conservative fallback if nothing detected
    s_detected_color_mode = COLOR_MODE_16;
#endif
}

/**
 * @brief Converts an RGB color to a 256-color ANSI code.
 * @param r Red component (0-255).
 * @param g Green component (0-255).
 * @param b Blue component (0-255).
 * @return The 256-color ANSI code.
 */
static int rgb_to_256(unsigned char r, unsigned char g, unsigned char b) {
    // Improved grayscale range (232-255)
    if (r == g && g == b) {
        if (r < 3) return 16;        // Black
        if (r > 252) return 231;     // White
        return 232 + (r - 3) / 10;   // 24 shades of gray
    }
    
    // Optimized 6x6x6 color cube (16-231)
    int ri = min(5, (r * 6) / 256);
    int gi = min(5, (g * 6) / 256);
    int bi = min(5, (b * 6) / 256);
    
    return 16 + (ri * 36) + (gi * 6) + bi;
}

/**
 * @brief Converts an RGB color to a 16-color ANSI code.
 * @param r Red component (0-255).
 * @param g Green component (0-255).
 * @param b Blue component (0-255).
 * @return The 16-color ANSI code.
 */
static int rgb_to_16(unsigned char r, unsigned char g, unsigned char b) {
    int intensity = (r > 128 || g > 128 || b > 128) ? 8 : 0; // Bright bit
    int r_bit = (r > 128) ? 4 : 0; // Red bit (2^2)
    int g_bit = (g > 128) ? 2 : 0; // Green bit (2^1)
    int b_bit = (b > 128) ? 1 : 0; // Blue bit (2^0)
    return intensity + r_bit + g_bit + b_bit;
}

/**
 * @brief Initializes the ANSI color code caches for 16-color and 256-color modes.
 * Allocates memory for each cached string.
 */
void init_ansi_cache(void) {
    // For 16-color mode
    for (int i = 0; i < 16; i++) {
        s_ansi_cache_16[i] = (char*)malloc(16); // Max 16 chars for "\033[107m "
        if (!s_ansi_cache_16[i]) {
            LOG_ERROR("%s", "Failed to allocate memory for 16-color ANSI cache entry.");
            // Continue, but rendering might fall back to default
            continue;
        }
        if (i < 8) { // Normal colors (0-7) -> 40-47 (background)
            sprintf(s_ansi_cache_16[i], "\033[4%dm", i);
        } else { // Bright colors (8-15) -> 100-107 (background)
            sprintf(s_ansi_cache_16[i], "\033[10%dm", i - 8);
        }
    }
    
    // For 256-color mode
    for (int i = 0; i < 256; i++) {
        s_ansi_cache_256[i] = (char*)malloc(16); // Max 16 chars for "\033[48;5;255m "
        if (!s_ansi_cache_256[i]) {
            LOG_ERROR("%s", "Failed to allocate memory for 256-color ANSI cache entry.");
            // Continue, but rendering might fall back to default
            continue;
        }
        sprintf(s_ansi_cache_256[i], "\033[48;5;%dm", i);
    }
}

/**
 * @brief Frees all memory allocated for the ANSI color code caches.
 */
void free_ansi_cache(void) {
    for (int i = 0; i < 16; i++) {
        if (s_ansi_cache_16[i]) {
            free(s_ansi_cache_16[i]);
            s_ansi_cache_16[i] = NULL;
        }
    }
    for (int i = 0; i < 256; i++) {
        if (s_ansi_cache_256[i]) {
            free(s_ansi_cache_256[i]);
            s_ansi_cache_256[i] = NULL;
        }
    }
}

/**
 * @brief Formats an ANSI color escape code into a buffer based on the detected color mode.
 * Uses cached strings for 16 and 256 color modes for performance.
 * @param buf The character buffer to write the ANSI code into.
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 * @param mode The detected ColorMode.
 * @return The number of characters written to the buffer.
 */
static int format_ansi_color_code(char* buf, unsigned char r, unsigned char g, unsigned char b, ColorMode mode) {
    char* start = buf;
    switch (mode) {
        case COLOR_MODE_TRUE_COLOR:
            // \033[48;2;R;G;Bm (background 24-bit true color)
            // Using sprintf directly as it's typically fast enough for true color
            // and avoids the complexity of caching 16M strings.
            buf += sprintf(buf, "\033[48;2;%d;%d;%dm ", r, g, b);
            break;
        case COLOR_MODE_256: {
            // \033[48;5;###m (background 256 color)
            int color_code = rgb_to_256(r, g, b);
            char* cached = s_ansi_cache_256[color_code];
            if (cached) {
                size_t len = strlen(cached);
                memcpy(buf, cached, len);
                buf += len;
                *buf = ' '; // Add space after color code
                buf++;
            } else {
                // Fallback if cache entry is missing (should not happen if init_ansi_cache worked)
                buf += sprintf(buf, "\033[48;5;%dm ", color_code);
            }
            break;
        }
        case COLOR_MODE_16: {
            // \033[4#m (background 16 color)
            int color_code = rgb_to_16(r, g, b);
            char* cached = s_ansi_cache_16[color_code];
            if (cached) {
                size_t len = strlen(cached);
                memcpy(buf, cached, len);
                buf += len;
                *buf = ' '; // Add space after color code
                buf++;
            } else {
                // Fallback if cache entry is missing
                buf += sprintf(buf, "\033[4%dm ", color_code);
            }
            break;
        }
        default:
            // Fallback: just a space character
            *buf = ' ';
            buf++;
            break;
    }
    return buf - start;
}

/**
 * @brief Renders the image data to the terminal using ANSI escape codes.
 * Supports different color modes. No screen clearing or cursor manipulation.
 *
 * @param img_data Pointer to the pixel data of the image to render.
 * @param width The width of the image to render (in pixels/terminal columns).
 * @param height The height of the image to render (in pixels/terminal rows).
 * @param channels The number of color channels (e.g., 3 for RGB, 4 for RGBA).
 * @param bg_r Red component of background color for alpha blending.
 * @param bg_g Green component of background color for alpha blending.
 * @param bg_b Blue component of background color for alpha blending.
 */
void render_image(unsigned char *img_data, int width, int height, int channels, unsigned char bg_r, unsigned char bg_g, unsigned char bg_b) {
    // Removed: printf("\033[H\033[J"); // ANSI escape code to clear screen and move cursor to home position
    // This was removed in v0.1.7 to prevent interference with complex terminal prompts and rendering artifacts.

    // Calculate buffer size and check for overflow
    // Max chars per pixel: True Color (20 chars) + space (1 char) = 21 chars
    // Max chars per pixel: 256-color (12 chars) + space (1 char) = 13 chars
    // Max chars per pixel: 16-color (8 chars) + space (1 char) = 9 chars
    size_t max_pixel_size;
    switch (s_detected_color_mode) {
        case COLOR_MODE_TRUE_COLOR: max_pixel_size = 21; break;
        case COLOR_MODE_256: max_pixel_size = 13; break;
        case COLOR_MODE_16: max_pixel_size = 9; break;
        default: max_pixel_size = 2; break; // Fallback for ' '
    }
    
    size_t buffer_size_per_line = (size_t)width * max_pixel_size + 32; // +32 for reset code and margin

    // Check for multiplication overflow
    if (buffer_size_per_line / max_pixel_size < (size_t)width) { 
        LOG_ERROR("Buffer size calculation overflow for width %d. Cannot render.", width);
        return;
    }
    
    char *buffer = (char *)malloc(buffer_size_per_line);
    if (!buffer) {
        LOG_ERROR("%s", "Failed to allocate render buffer.");
        return;
    }

    // Progress bar is explicitly disabled.
    // int show_progress = 0; 

    // Detect color support once before rendering loop
    if (s_detected_color_mode == COLOR_MODE_UNKNOWN) {
        detect_color_support();
    }
    
    // Removed gamma correction logic to revert to previous color handling
    // float gamma_factor = 2.2f; 
    // float inv_gamma = 1.0f / gamma_factor; 

    for (int y = 0; y < height; y++) {
        int buf_pos = 0;
        
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * channels;
            unsigned char r, g, b;

            if (channels == 4) { // Handle alpha channel by blending with a specified background color
                float alpha_norm = img_data[idx+3] / 255.0f;
                r = (unsigned char)(img_data[idx] * alpha_norm + bg_r * (1.0f - alpha_norm));
                g = (unsigned char)(img_data[idx+1] * alpha_norm + bg_g * (1.0f - alpha_norm));
                b = (unsigned char)(img_data[idx+2] * alpha_norm + bg_b * (1.0f - alpha_norm));
            } else { // 3 channels or less, no alpha
                r = img_data[idx];
                g = img_data[idx+1];
                b = img_data[idx+2];
            }

            // Removed gamma correction (sRGB to linear approximation)
            // r = (unsigned char)(fmax(0, fmin(255, 255.0f * powf(r_orig / 255.0f, inv_gamma) + 0.5f)));
            // g = (unsigned char)(fmax(0, fmin(255, 255.0f * powf(g_orig / 255.0f, inv_gamma) + 0.5f)));
            // b = (unsigned char)(fmax(0, fmin(255, 255.0f * powf(b_orig / 255.0f, inv_gamma) + 0.5f)));

            buf_pos += format_ansi_color_code(buffer + buf_pos, r, g, b, s_detected_color_mode);
        }
        
        // Add reset color and newline
        buf_pos += sprintf(buffer + buf_pos, "\033[0m\n");
        fwrite(buffer, 1, buf_pos, stdout);
    }
    
    free(buffer);
    fflush(stdout); // Ensure immediate output to the terminal
}

/**
 * @brief Resizes a source rectangle of an image using bilinear interpolation.
 *
 * @param img_data Pointer to the source image's pixel data.
 * @param orig_w Original width of the source image.
 * @param orig_h Original height of the source image.
 * @param orig_channels Number of channels in the source image (e.g., 3 for RGB, 4 for RGBA).
 * @param src_x X-coordinate of the top-left corner of the source rectangle.
 * @param src_y Y-coordinate of the top-left corner of the source rectangle.
 * @param src_w Width of the source rectangle.
 * @param src_h Height of the source rectangle.
 * @param new_w Desired new width for the resized output.
 * @param new_h Desired new height for the resized output.
 * @return A pointer to the newly allocated pixel data for the resized image, or NULL on error.
 * The caller is responsible for freeing this memory.
 */
unsigned char* resize_image_bilinear(unsigned char * restrict img_data, int orig_w, int orig_h, int orig_channels,
                                     int src_x, int src_y, int src_w, int src_h,
                                     int new_w, int new_h) {
    if (!img_data || new_w <= 0 || new_h <= 0 || src_w <= 0 || src_h <= 0) {
        LOG_ERROR("%s", "Invalid input for resize_image_bilinear.");
        return NULL;
    }

    uint64_t data_size_64 = (uint64_t)new_w * new_h * orig_channels;
    if (data_size_64 > SIZE_MAX) {
        LOG_ERROR("Image too large: %dx%dx%d (max: %zu)", new_w, new_h, orig_channels, SIZE_MAX);
        return NULL;
    }
    size_t data_size = (size_t)data_size_64;

    // Use malloc. For highly optimized SIMD, posix_memalign might be used for aligned memory.
    unsigned char * restrict resized = (unsigned char*)malloc(data_size);
    if (!resized) {
        LOG_ERROR("Failed to allocate memory for resized image (size %zu).", data_size);
        return NULL;
    }

    float x_scale = (float)src_w / new_w;
    float y_scale = (float)src_h / new_h;

    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            float ox = src_x + x * x_scale;
            float oy = src_y + y * y_scale;

            int x1 = (int)ox;
            int y1 = (int)oy;
            int x2 = x1 + 1;
            int y2 = y1 + 1;

            float dx = ox - x1;
            float dy = oy - y1;

            // Clamp coordinates to original image boundaries for safety
            x1 = x1 < 0 ? 0 : (x1 >= orig_w ? orig_w - 1 : x1);
            y1 = y1 < 0 ? 0 : (y1 >= orig_h ? orig_h - 1 : y1);
            x2 = x2 < 0 ? 0 : (x2 >= orig_w ? orig_w - 1 : x2);
            y2 = y2 < 0 ? 0 : (y2 >= orig_h ? orig_h - 1 : y2);
            
            // Optimized bilinear interpolation loop
            // This loop is a candidate for SIMD vectorization (SSE2/NEON)
            for (int c = 0; c < orig_channels; c++) {
                // Calculate indices once per channel
                int idx11 = (y1 * orig_w + x1) * orig_channels + c;
                int idx21 = (y1 * orig_w + x2) * orig_channels + c;
                int idx12 = (y2 * orig_w + x1) * orig_channels + c;
                int idx22 = (y2 * orig_w + x2) * orig_channels + c;
                
                // Bilinear interpolation formula
                float val_x1 = img_data[idx11] * (1.0f - dx) + img_data[idx21] * dx;
                float val_x2 = img_data[idx12] * (1.0f - dx) + img_data[idx22] * dx;
                float final_val = val_x1 * (1.0f - dy) + val_x2 * dy;
                
                // Store result, adding 0.5f for proper rounding
                resized[(y * new_w + x) * orig_channels + c] = (unsigned char)(final_val + 0.5f);
            }

            /*
            // Placeholder for SIMD optimization (example for 4 channels, needs careful implementation)
            #if defined(__SSE2__) && defined(__GNUC__) // GCC/Clang with SSE2
            if (orig_channels == 4) { // Example for RGBA
                __m128i p11_vec = _mm_cvtsi32_si128(*(uint32_t*)&img_data[idx11]); // Load 4 bytes (RGBA)
                // ... convert to float, perform vector math ...
            }
            #elif defined(__ARM_NEON) // ARM NEON
            if (orig_channels == 4) { // Example for RGBA
                uint8x8_t p_row1 = vld1_u8(&img_data[y1 * orig_w * orig_channels + x1 * orig_channels]);
                // ... convert to float, perform vector math ...
            }
            #endif
            */
        }
    }
    return resized;
}

/**
 * @brief Calculates the optimal display dimensions (width and height) for the image
 * based on terminal size, original image dimensions, and a zoom factor.
 * Adjusts for terminal character aspect ratio.
 *
 * @param img_orig_width The original width of the image in pixels (or source width if cropping).
 * @param img_orig_height The original height of the image in pixels (or source height if cropping).
 * @param zoom_factor The desired zoom level (1.0f means fit to terminal).
 * @param display_width Pointer to store the calculated display width in columns.
 * @param display_height Pointer to store the calculated display height in rows.
 */
void calculate_display_dimensions(int img_orig_width, int img_orig_height, float zoom_factor,
                                  int *display_width, int *display_height) {
    int terminal_width, terminal_height;
    get_terminal_size(&terminal_width, &terminal_height);

    int usable_terminal_height = terminal_height - 2; // Reserve 2 rows for prompt/status
    if (usable_terminal_height <= 0) usable_terminal_height = 1;

    if (img_orig_width <= 0 || img_orig_height <= 0) {
        *display_width = terminal_width;
        *display_height = usable_terminal_height;
        LOG_WARNING("Invalid original image dimensions, using full terminal: %dx%d", *display_width, *display_height);
        return;
    }

    // Image's original pixel aspect ratio (width / height)
    float image_pixel_aspect_ratio = (float)img_orig_width / img_orig_height;

    // Terminal's effective pixel aspect ratio (considering character cell shape)
    // effective_pixel_aspect_ratio = (terminal_width * char_width) / (terminal_height * char_height)
    // Since char_width is 1 unit and char_height is TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO units,
    // effective_terminal_pixel_aspect_ratio = terminal_width / (usable_terminal_height * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO)
    float effective_terminal_pixel_aspect_ratio = (float)terminal_width / (usable_terminal_height * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);

    int calculated_width_cols, calculated_height_rows;

    // Determine if image is wider or taller than the effective terminal space
    if (image_pixel_aspect_ratio > effective_terminal_pixel_aspect_ratio) {
        // Image is wider relative to the effective terminal area, so scale by width
        calculated_width_cols = (int)(terminal_width * zoom_factor);
        // Calculate rows needed to maintain image aspect ratio in effective pixels
        // calculated_height_rows = (calculated_width_cols / image_pixel_aspect_ratio) / TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO
        calculated_height_rows = (int)((calculated_width_cols / image_pixel_aspect_ratio) / TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);
    } else {
        // Image is taller relative to the effective terminal area, so scale by height
        calculated_height_rows = (int)(usable_terminal_height * zoom_factor);
        // Calculate cols needed to maintain image aspect ratio in effective pixels
        // calculated_width_cols = (calculated_height_rows * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO) * image_pixel_aspect_ratio
        calculated_width_cols = (int)((calculated_height_rows * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO) * image_pixel_aspect_ratio);
    }
    
    // Ensure minimum dimensions
    if (calculated_width_cols <= 0) calculated_width_cols = 1;
    if (calculated_height_rows <= 0) calculated_height_rows = 1;

    // Final clamping to ensure it doesn't exceed terminal size after zoom
    if (calculated_width_cols > terminal_width) {
        calculated_width_cols = terminal_width;
        calculated_height_rows = (int)((calculated_width_cols / image_pixel_aspect_ratio) / TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);
        if (calculated_height_rows <= 0) calculated_height_rows = 1;
    }
    if (calculated_height_rows > usable_terminal_height) {
        calculated_height_rows = usable_terminal_height;
        calculated_width_cols = (int)((calculated_height_rows * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO) * image_pixel_aspect_ratio);
        if (calculated_width_cols <= 0) calculated_width_cols = 1;
    }

    // Re-ensure minimums after potential clamping
    if (calculated_width_cols <= 0) calculated_width_cols = 1;
    if (calculated_height_rows <= 0) calculated_height_rows = 1;

    *display_width = calculated_width_cols;
    *display_height = calculated_height_rows;
    LOG_INFO("Calculated display dimensions: %dx%d (original image: %dx%d, zoom: %.2f, char H/W ratio: %.2f)", 
             *display_width, *display_height, img_orig_width, img_orig_height, zoom_factor, TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);
}

/**
 * @brief Flips an image horizontally.
 * @param img_data Pointer to the source image data.
 * @param w Width of the image.
 * @param h Height of the image.
 * @param c Number of channels.
 * @return Pointer to the new flipped image data, or NULL on failure. Caller must free.
 */
unsigned char* flip_image_horizontal(unsigned char * restrict img_data, int w, int h, int c) {
    if (!img_data) return NULL;
    size_t data_size = (size_t)w * h * c;
    unsigned char * restrict flipped_data = (unsigned char*)malloc(data_size);
    if (!flipped_data) {
        LOG_ERROR("%s", "Failed to allocate memory for horizontal flip.");
        return NULL;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int channel = 0; channel < c; channel++) {
                flipped_data[(y * w + x) * c + channel] = img_data[(y * w + (w - 1 - x)) * c + channel];
            }
        }
    }
    return flipped_data;
}

/**
 * @brief Flips an image vertically.
 * @param img_data Pointer to the source image data.
 * @param w Width of the image.
 * @param h Height of the image.
 * @param c Number of channels.
 * @return Pointer to the new flipped image data, or NULL on failure. Caller must free.
 */
unsigned char* flip_image_vertical(unsigned char * restrict img_data, int w, int h, int c) {
    if (!img_data) return NULL;
    size_t data_size = (size_t)w * h * c;
    unsigned char * restrict flipped_data = (unsigned char*)malloc(data_size);
    if (!flipped_data) {
        LOG_ERROR("%s", "Failed to allocate memory for vertical flip.");
        return NULL;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int channel = 0; channel < c; channel++) {
                flipped_data[(y * w + x) * c + channel] = img_data[((h - 1 - y) * w + x) * c + channel];
            }
        }
    }
    return flipped_data;
}

/**
 * @brief Rotates an image 90 degrees clockwise.
 * @param img_data Pointer to the source image data.
 * @param w Pointer to the width (will be updated to new height).
 * @param h Pointer to the height (will be updated to new width).
 * @param c Number of channels.
 * @return Pointer to the new rotated image data, or NULL on failure. Caller must free.
 */
unsigned char* rotate_image_90_cw(unsigned char * restrict img_data, int *w, int *h, int c) {
    if (!img_data) return NULL;
    int original_w = *w;
    int original_h = *h;
    
    // New dimensions: width becomes old height, height becomes old width
    int new_w = original_h;
    int new_h = original_w;

    size_t data_size = (size_t)new_w * new_h * c;
    unsigned char * rotated_data = (unsigned char*)malloc(data_size);
    if (!rotated_data) {
        LOG_ERROR("%s", "Failed to allocate memory for 90-degree rotation.");
        return NULL;
    }

    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            // Map new (x,y) to original (ox, oy)
            int ox = y;
            int oy = original_w - 1 - x;
            
            for (int channel = 0; channel < c; channel++) {
                rotated_data[(y * new_w + x) * c + channel] = img_data[(oy * original_w + ox) * c + channel];
            }
        }
    }
    *w = new_w;
    *h = new_h;
    return rotated_data;
}

/**
 * @brief Rotates an image 180 degrees.
 * @param img_data Pointer to the source image data.
 * @param w Width of the image.
 * @param h Height of the image.
 * @param c Number of channels.
 * @return Pointer to the new rotated image data, or NULL on failure. Caller must free.
 */
unsigned char* rotate_image_180(unsigned char * restrict img_data, int w, int h, int c) {
    if (!img_data) return NULL;
    size_t data_size = (size_t)w * h * c;
    unsigned char * restrict rotated_data = (unsigned char*)malloc(data_size);
    if (!rotated_data) {
        LOG_ERROR("%s", "Failed to allocate memory for 180-degree rotation.");
        return NULL;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int channel = 0; channel < c; channel++) {
                rotated_data[(y * w + x) * c + channel] = img_data[((h - 1 - y) * w + (w - 1 - x)) * c + channel];
            }
        }
    }
    return rotated_data;
}

// Add __attribute__((used)) to prevent linker from optimizing it out
__attribute__((used))
void free_image_cache(void) {
    if (s_image_cache) {
        for (int i = 0; i < s_cache_size; i++) {
            free(s_image_cache[i].data);
        }
        free(s_image_cache);
        s_image_cache = NULL;
        s_cache_size = 0;
    }
}


/**
 * @brief Main function of the PIT program.
 * Parses command-line arguments, loads and renders the image.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of strings containing the command-line arguments.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    // Removed: signal(SIGINT, handle_signal);
    // Removed: signal(SIGTERM, handle_signal);
    // Removed: atexit(disable_raw_mode); // No raw mode to disable (this was the fix for broken terminal)

#ifdef _WIN32
    // Enable UTF-8 output on Windows console
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Command-line argument processing variables
    char *filename = NULL;
    int opt_target_width = 0;  // User specified output width
    int opt_target_height = 0; // User specified output height
    float cli_zoom_factor = 1.0f; // Zoom factor for initial view
    int offset_x = 0; // View offset in original image pixels
    int offset_y = 0; // View offset in original image pixels
    bool flip_h = false;
    bool flip_v = false;
    int rotate_degrees = 0; // 0, 90, 180, 270
    unsigned char bg_r = 0, bg_g = 0, bg_b = 0; // Default background: black
    // Removed: bool force_true_color = false; // Removed this flag

    // Initialize current_img_data to NULL to prevent uninitialized use warnings
    unsigned char *current_img_data = NULL;

    // Suppress unused variable warnings for cache-related globals
    (void)s_image_cache;
    (void)s_cache_size;

    // Detect color support early
    detect_color_support();
    // Initialize ANSI color caches
    init_ansi_cache();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            goto cleanup_and_exit;
        } 
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("PIT v0.5\n"); // Updated version number
            goto cleanup_and_exit;
        } 
        else if (strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-w") == 0) {
            if (i+1 < argc) opt_target_width = atoi(argv[++i]);
        } 
        else if (strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-H") == 0) {
            if (i+1 < argc) opt_target_height = atoi(argv[++i]);
        } 
        else if (strcmp(argv[i], "--zoom") == 0) {
            if (i+1 < argc) cli_zoom_factor = atof(argv[++i]);
            if (cli_zoom_factor <= 0) cli_zoom_factor = 1.0f; // Prevent zero or negative zoom
        }
        else if (strcmp(argv[i], "--offset-x") == 0) {
            if (i+1 < argc) offset_x = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--offset-y") == 0) {
            if (i+1 < argc) offset_y = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--flip-h") == 0) {
            flip_h = true;
        }
        else if (strcmp(argv[i], "--flip-v") == 0) {
            flip_v = true;
        }
        else if (strcmp(argv[i], "--rotate") == 0) {
            if (i+1 < argc) rotate_degrees = atoi(argv[++i]);
            if (rotate_degrees % 90 != 0) {
                LOG_WARNING("Rotation degrees must be a multiple of 90. Using %d.", rotate_degrees - (rotate_degrees % 90));
                rotate_degrees = rotate_degrees - (rotate_degrees % 90);
            }
            rotate_degrees = (rotate_degrees % 360 + 360) % 360; // Normalize to 0, 90, 180, 270
        }
        else if (strcmp(argv[i], "--bg") == 0) {
            if (i+1 < argc) {
                if (strcmp(argv[++i], "black") == 0) {
                    bg_r = 0; bg_g = 0; bg_b = 0;
                } else if (strcmp(argv[i], "white") == 0) {
                    bg_r = 255; bg_g = 255; bg_b = 255;
                } else {
                    LOG_WARNING("Unsupported background color '%s'. Using default black.", argv[i]);
                }
            }
        }
        // Removed: else if (strcmp(argv[i], "--true-color") == 0 || strcmp(argv[i], "-T") == 0) {
        // Removed:     force_true_color = true;
        // Removed: }
        else {
            // Handle multiple file arguments: only the first is used, warn about others.
            if (filename == NULL) {
                filename = argv[i];
            } else {
                LOG_WARNING("Multiple image files specified. Using '%s' and ignoring '%s'.", filename, argv[i]);
            }
        }
    }

    // Removed: Apply forced true color mode if specified
    // Removed: if (force_true_color) {
    // Removed:     s_detected_color_mode = COLOR_MODE_TRUE_COLOR;
    // Removed:     LOG_INFO("Forcing 24-bit true color mode.");
    // Removed: }


    if (!filename) {
        LOG_ERROR("%s", "No image file specified.");
        print_help();
        goto cleanup_and_exit;
    }

    // --- Memory warning for large images ---
    // Estimate max memory needed: original + 3 intermediate transformations + final resized
    size_t estimated_max_mem = (size_t)s_original_width * s_original_height * s_original_channels * 5; // Factor of 5 for safety
    if (estimated_max_mem > 100 * 1024 * 1024) { // >100MB
        LOG_WARNING("Large image detected (%dx%d). Estimated memory usage: %.2f MB. Consider using --width/--height to limit output size.", 
                    s_original_width, s_original_height, (float)estimated_max_mem / (1024 * 1024));
    }


    // Load the image
    s_original_image_data = stbi_load(filename, &s_original_width, &s_original_height, &s_original_channels, 0); 
    
    if (!s_original_image_data) {
        const char* reason = stbi_failure_reason();
        const char* msg = reason ? reason : "Unknown error";
        
        // Specific advice for common errors
        if(strstr(msg, "unknown")) {
            LOG_ERROR("Unsupported image format or corrupt file header for '%s'.", filename);
        } else if(strstr(msg, "too large")) {
            LOG_ERROR("Image dimensions exceed internal limits for '%s'.", filename);
        } else {
            LOG_ERROR("Failed to load image '%s': %s", filename, msg);
        }
        goto cleanup_and_exit;
    }

    // Validate original image dimensions
    if (s_original_width <= 0 || s_original_height <= 0) {
        LOG_ERROR("Invalid image dimensions (%dx%d) for '%s'.", s_original_width, s_original_height, filename);
        goto cleanup_and_exit;
    }

    // Assign original image data to current_img_data after successful load and validation
    current_img_data = s_original_image_data;

    // --- Image Processing Pipeline ---
    int current_img_w = s_original_width;
    int current_img_h = s_original_height;
    int current_img_c = s_original_channels; // Channels don't change during transforms

    // Apply transformations (flip, rotate)
    unsigned char *temp_data = NULL;

    if (flip_h) {
        temp_data = flip_image_horizontal(current_img_data, current_img_w, current_img_h, current_img_c);
        if (!temp_data) goto cleanup_and_exit;
        if (current_img_data != s_original_image_data) free(current_img_data); // Free previous temp data
        current_img_data = temp_data;
    }
    if (flip_v) {
        temp_data = flip_image_vertical(current_img_data, current_img_w, current_img_h, current_img_c);
        if (!temp_data) goto cleanup_and_exit;
        if (current_img_data != s_original_image_data) free(current_img_data);
        current_img_data = temp_data;
    }
    
    if (rotate_degrees != 0) {
        if (rotate_degrees == 90 || rotate_degrees == 270) { // 90 or 270 degrees
            int rotations = rotate_degrees / 90;
            for (int i = 0; i < rotations; ++i) {
                temp_data = rotate_image_90_cw(current_img_data, &current_img_w, &current_img_h, current_img_c);
                if (!temp_data) goto cleanup_and_exit;
                if (current_img_data != s_original_image_data) free(current_img_data);
                current_img_data = temp_data;
            }
        } else if (rotate_degrees == 180) {
            temp_data = rotate_image_180(current_img_data, current_img_w, current_img_h, current_img_c);
            if (!temp_data) goto cleanup_and_exit;
            if (current_img_data != s_original_image_data) free(current_img_data);
            current_img_data = temp_data;
        }
    }

    // --- Define Source Rectangle for Resizing (based on zoom and offset) ---
    int src_x = offset_x;
    int src_y = offset_y;
    int src_w = current_img_w;
    int src_h = current_img_h;

    // Apply zoom to the source rectangle dimensions
    // A cli_zoom_factor > 1 means zoom in (smaller src_w/h portion of the image)
    // A cli_zoom_factor < 1 means zoom out (larger src_w/h portion, potentially showing outside image)
    src_w = (int)(current_img_w / cli_zoom_factor);
    src_h = (int)(current_img_h / cli_zoom_factor);

    // Clamp source dimensions to ensure they are at least 1x1 and not larger than current_img_w/h
    if (src_w <= 0) src_w = 1;
    if (src_h <= 0) src_h = 1;
    if (src_w > current_img_w) src_w = current_img_w;
    if (src_h > current_img_h) src_h = current_img_h;

    // Clamp source offsets to ensure the rectangle stays within current_img_w/h
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x + src_w > current_img_w) src_x = current_img_w - src_w;
    if (src_y + src_h > current_img_h) src_y = current_img_h - src_h;
    // Re-clamp if src_w/h was adjusted (e.g., if src_w was initially > current_img_w)
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;

    LOG_INFO("Source rectangle for resize: x=%d, y=%d, w=%d, h=%d (from image %dx%d)", src_x, src_y, src_w, src_h, current_img_w, current_img_h);


    // --- Calculate Final Display Dimensions for Terminal ---
    int final_display_width;
    int final_display_height;

    if (opt_target_width > 0 || opt_target_height > 0) {
        // User specified exact dimensions
        final_display_width = opt_target_width > 0 ? opt_target_width : 1;
        final_display_height = opt_target_height > 0 ? opt_target_height : 1;

        // If only one dimension is specified, calculate the other to maintain aspect ratio
        if (opt_target_width > 0 && opt_target_height <= 0) {
            // Calculate height based on new width, original image aspect ratio, and char ratio
            final_display_height = (int)(src_h * (final_display_width / (float)src_w) / TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);
        } else if (opt_target_height > 0 && opt_target_width <= 0) {
            // Calculate width based on new height, original image aspect ratio, and char ratio
            final_display_width = (int)(src_w * (final_display_height / (float)src_h) * TERMINAL_CHAR_HEIGHT_TO_WIDTH_RATIO);
        }
        // Ensure minimums
        if (final_display_width <= 0) final_display_width = 1;
        if (final_display_height <= 0) final_display_height = 1;

        LOG_INFO("User specified dimensions: %dx%d (calculated: %dx%d)", opt_target_width, opt_target_height, final_display_width, final_display_height);
    } else {
        // Auto-size to terminal, considering zoom and offset
        calculate_display_dimensions(src_w, src_h, 1.0f, &final_display_width, &final_display_height);
    }

    // Get terminal size again to clamp final dimensions, even if user specified
    int terminal_width, terminal_height;
    get_terminal_size(&terminal_width, &terminal_height);
    int usable_terminal_height = terminal_height - 2; // Account for status bar
    if (usable_terminal_height <= 0) usable_terminal_height = 1;

    // Final clamping to ensure it doesn't exceed terminal size
    if (final_display_width > terminal_width) final_display_width = terminal_width;
    if (final_display_height > usable_terminal_height) final_display_height = usable_terminal_height;
    if (final_display_width <= 0) final_display_width = 1;
    if (final_display_height <= 0) final_display_height = 1;

    LOG_INFO("Final display dimensions for rendering: %dx%d", final_display_width, final_display_height);

    // --- Resize and Render ---
    unsigned char *rendered_img_data = resize_image_bilinear(current_img_data, current_img_w, current_img_h, current_img_c,
                                                             src_x, src_y, src_w, src_h,
                                                             final_display_width, final_display_height);
    
    if (!rendered_img_data) {
        LOG_ERROR("%s", "Failed to prepare image for display (resize failed).");
        goto cleanup_and_exit;
    }

    render_image(rendered_img_data, final_display_width, final_display_height, current_img_c, bg_r, bg_g, bg_b);

    // --- Cleanup ---
    free(rendered_img_data); // Free the resized image data

cleanup_and_exit:
    // Free any intermediate image data from transformations
    // Only free if it's not the original data (which is freed by stbi_image_free)
    if (current_img_data != s_original_image_data && current_img_data != NULL) {
        free(current_img_data);
    }
    // Free original image memory allocated by stb_image
    if (s_original_image_data) {
        stbi_image_free(s_original_image_data);
        s_original_image_data = NULL;
    }
    // Free all cached image data (if any was added, though not expected in this mode)
    free_image_cache();
    // Free ANSI color caches
    free_ansi_cache();
    
    return 0; // Return 0 for success, non-zero for error
}

