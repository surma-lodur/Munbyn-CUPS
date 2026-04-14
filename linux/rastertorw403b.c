#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

#include <cups/cups.h>
#include <cups/raster.h>

#ifdef HAVE_PPD
#include <cups/ppd.h>
#else
typedef void ppd_file_t;
#endif

enum {
    HSE_STATE_IDLE = 0,
    HSE_STATE_SEARCH,
    HSE_STATE_YIELD_TAG,
    HSE_STATE_YIELD_LITERAL,
    HSE_STATE_YIELD_BR_INDEX,
    HSE_STATE_YIELD_BR_LENGTH,
    HSE_STATE_SAVE_BACKLOG,
    HSE_STATE_FLUSH_BITS,
    HSE_STATE_DONE,
};

#define HSE_WINDOW_SIZE 0x800u
#define HSE_LOOKAHEAD_SIZE 0x10u
#define HSE_MIN_MATCH 3u
#define HSE_MAX_MATCH HSE_LOOKAHEAD_SIZE
#define HSE_BR_INDEX_BITS 11u
#define HSE_BR_LENGTH_BITS 4u

typedef struct {
    unsigned char *buf;
    size_t cap;
    size_t len;
} hse_output_t;

typedef struct {
    unsigned char *input;
    int *prev;
    size_t input_size;
    size_t input_cap;
    size_t prev_cap;
    size_t scan_index;
    size_t match_length;
    size_t match_distance;
    unsigned int outgoing_bits;
    unsigned char outgoing_count;
    unsigned char bit_buffer;
    unsigned char bit_mask;
    unsigned char finishing;
    unsigned char indexed;
    unsigned char state;
} heatshrink_encoder_t;

static heatshrink_encoder_t _hse = { 0 };

static size_t _get_input_buffer_size(void) {
    return HSE_WINDOW_SIZE;
}

static size_t _get_input_offset(const heatshrink_encoder_t *encoder) {
    (void)encoder;
    return 0;
}

static size_t _get_lookahead_size(const heatshrink_encoder_t *encoder) {
    (void)encoder;
    return HSE_LOOKAHEAD_SIZE;
}

static int _is_finishing(const heatshrink_encoder_t *encoder) {
    return encoder && encoder->finishing;
}

static int _can_take_byte(const hse_output_t *output) {
    return output && output->len < output->cap;
}

static void _do_indexing(heatshrink_encoder_t *encoder) {
    if (!encoder) return;
    if (encoder->prev_cap < encoder->input_size) {
        int *next_prev = (int *)realloc(encoder->prev, encoder->input_size * sizeof(int));
        if (!next_prev) return;
        encoder->prev = next_prev;
        encoder->prev_cap = encoder->input_size;
    }

    int last[256];
    for (int i = 0; i < 256; ++i) last[i] = -1;
    for (size_t i = 0; i < encoder->input_size; ++i) {
        unsigned char byte = encoder->input[i];
        encoder->prev[i] = last[byte];
        last[byte] = (int)i;
    }
    encoder->indexed = 1;
}

static size_t _find_longest_match(heatshrink_encoder_t *encoder, size_t pos, size_t max_len, size_t *match_len) {
    if (!encoder || !match_len || pos >= encoder->input_size || !encoder->indexed) {
        if (match_len) *match_len = 0;
        return 0;
    }

    size_t best_len = 0;
    size_t best_dist = 0;
    int candidate = encoder->prev[pos];
    size_t chain_budget = 128;
    size_t window = _get_input_buffer_size();

    while (candidate >= 0 && chain_budget-- > 0) {
        size_t cand = (size_t)candidate;
        if (pos <= cand) break;
        size_t dist = pos - cand;
        if (dist > window) break;

        size_t len = 0;
        while (len < max_len && pos + len < encoder->input_size && encoder->input[cand + len] == encoder->input[pos + len]) {
            len++;
        }

        if (len > best_len) {
            best_len = len;
            best_dist = dist;
            if (best_len == max_len) break;
        }

        candidate = encoder->prev[cand];
    }

    if (best_len < HSE_MIN_MATCH) {
        *match_len = 0;
        return 0;
    }

    if (best_len > HSE_MAX_MATCH) {
        best_len = HSE_MAX_MATCH;
    }

    *match_len = best_len;
    return best_dist;
}

static void _save_backlog(heatshrink_encoder_t *encoder) {
    if (!encoder || encoder->scan_index == 0 || encoder->scan_index >= encoder->input_size) return;
    if (encoder->scan_index <= HSE_WINDOW_SIZE) return;

    size_t keep_from = encoder->scan_index - HSE_WINDOW_SIZE;
    size_t remaining = encoder->input_size - keep_from;
    memmove(encoder->input, encoder->input + keep_from, remaining);
    encoder->input_size = remaining;
    encoder->scan_index -= keep_from;
    encoder->indexed = 0;
}

static unsigned char _st_save_backlog(heatshrink_encoder_t *encoder) {
    _save_backlog(encoder);
    return HSE_STATE_SEARCH;
}

static void _push_bits(heatshrink_encoder_t *encoder, unsigned char count, unsigned int value, hse_output_t *output) {
    if (!encoder || !output || count == 0) return;

    if (count == 8 && encoder->bit_mask == 0x80 && _can_take_byte(output)) {
        output->buf[output->len++] = (unsigned char)value;
        return;
    }

    for (int bit = (int)count - 1; bit >= 0; --bit) {
        if ((value >> bit) & 1u) encoder->bit_buffer |= encoder->bit_mask;
        encoder->bit_mask >>= 1;
        if (encoder->bit_mask == 0) {
            if (!_can_take_byte(output)) return;
            output->buf[output->len++] = encoder->bit_buffer;
            encoder->bit_buffer = 0;
            encoder->bit_mask = 0x80;
        }
    }
}

static void _add_tag_bit(heatshrink_encoder_t *encoder, hse_output_t *output, unsigned int bit) {
    _push_bits(encoder, 1, bit ? 1u : 0u, output);
}

static void _push_literal_byte(heatshrink_encoder_t *encoder, hse_output_t *output) {
    if (!encoder || encoder->scan_index >= encoder->input_size) return;
    _push_bits(encoder, 8, encoder->input[encoder->scan_index], output);
    encoder->scan_index++;
}

static int _push_outgoing_bits(heatshrink_encoder_t *encoder, hse_output_t *output) {
    if (!encoder || encoder->outgoing_count == 0) return 0;
    _push_bits(encoder, encoder->outgoing_count, encoder->outgoing_bits, output);
    encoder->outgoing_count = 0;
    return 0;
}

static unsigned char _st_step_search(heatshrink_encoder_t *encoder) {
    if (!encoder) return HSE_STATE_DONE;
    if (!encoder->indexed) _do_indexing(encoder);

    size_t input_offset = _get_input_offset(encoder);
    if (encoder->scan_index < input_offset) encoder->scan_index = input_offset;

    if (encoder->scan_index >= encoder->input_size) {
        return _is_finishing(encoder) ? HSE_STATE_FLUSH_BITS : HSE_STATE_SAVE_BACKLOG;
    }

    size_t avail = encoder->input_size - encoder->scan_index;
    size_t lookahead = _get_lookahead_size(encoder);
    if (avail < lookahead) lookahead = avail;

    encoder->match_distance = _find_longest_match(encoder, encoder->scan_index, lookahead, &encoder->match_length);
    return HSE_STATE_YIELD_TAG;
}

static unsigned char _st_yield_tag_bit(heatshrink_encoder_t *encoder, hse_output_t *output) {
    if (!encoder || !output) return HSE_STATE_DONE;
    if (encoder->match_length == 0) {
        _add_tag_bit(encoder, output, 1u);
        return HSE_STATE_YIELD_LITERAL;
    }

    _add_tag_bit(encoder, output, 0u);
    encoder->outgoing_bits = (unsigned int)(encoder->match_distance - 1u);
    encoder->outgoing_count = HSE_BR_INDEX_BITS;
    return HSE_STATE_YIELD_BR_INDEX;
}

static unsigned char _st_yield_literal(heatshrink_encoder_t *encoder, hse_output_t *output) {
    _push_literal_byte(encoder, output);
    return HSE_STATE_SEARCH;
}

static unsigned char _st_yield_br_index(heatshrink_encoder_t *encoder, hse_output_t *output) {
    _push_outgoing_bits(encoder, output);
    encoder->outgoing_bits = (unsigned int)(encoder->match_length - 1u);
    encoder->outgoing_count = HSE_BR_LENGTH_BITS;
    return HSE_STATE_YIELD_BR_LENGTH;
}

static unsigned char _st_yield_br_length(heatshrink_encoder_t *encoder, hse_output_t *output) {
    _push_outgoing_bits(encoder, output);
    encoder->scan_index += encoder->match_length;
    encoder->match_length = 0;
    encoder->match_distance = 0;
    return HSE_STATE_SEARCH;
}

static unsigned char _st_flush_bit_buffer(heatshrink_encoder_t *encoder, hse_output_t *output) {
    if (!encoder || !output) return HSE_STATE_DONE;
    if (encoder->bit_mask != 0x80 && _can_take_byte(output)) {
        output->buf[output->len++] = encoder->bit_buffer;
        encoder->bit_buffer = 0;
        encoder->bit_mask = 0x80;
    }
    return HSE_STATE_DONE;
}

static void _heatshrink_encoder_reset(heatshrink_encoder_t *encoder) {
    if (!encoder) return;
    free(encoder->input);
    free(encoder->prev);
    memset(encoder, 0, sizeof(*encoder));
    encoder->bit_mask = 0x80;
}

static int _heatshrink_encoder_sink(heatshrink_encoder_t *encoder, const unsigned char *data, size_t len, size_t *sunk) {
    if (sunk) *sunk = 0;
    if (!encoder || !data) return -1;
    if (len == 0) return 0;

    if (encoder->input_size + len > encoder->input_cap) {
        size_t next_cap = encoder->input_cap ? encoder->input_cap : 256u;
        while (next_cap < encoder->input_size + len) next_cap *= 2u;
        unsigned char *next_input = (unsigned char *)realloc(encoder->input, next_cap);
        if (!next_input) return -1;
        encoder->input = next_input;
        encoder->input_cap = next_cap;
    }

    memcpy(encoder->input + encoder->input_size, data, len);
    encoder->input_size += len;
    encoder->indexed = 0;
    if (sunk) *sunk = len;
    return 0;
}

static int _heatshrink_encoder_poll(heatshrink_encoder_t *encoder, unsigned char *out, size_t out_cap, size_t *written) {
    if (written) *written = 0;
    if (!encoder || !out || out_cap == 0 || !written) return -1;

    hse_output_t output = { out, out_cap, 0 };
    if (encoder->state == HSE_STATE_IDLE) encoder->state = HSE_STATE_SEARCH;

    while (encoder->state != HSE_STATE_DONE) {
        switch (encoder->state) {
        case HSE_STATE_SEARCH:
            encoder->state = _st_step_search(encoder);
            break;
        case HSE_STATE_YIELD_TAG:
            encoder->state = _st_yield_tag_bit(encoder, &output);
            break;
        case HSE_STATE_YIELD_LITERAL:
            encoder->state = _st_yield_literal(encoder, &output);
            break;
        case HSE_STATE_YIELD_BR_INDEX:
            encoder->state = _st_yield_br_index(encoder, &output);
            break;
        case HSE_STATE_YIELD_BR_LENGTH:
            encoder->state = _st_yield_br_length(encoder, &output);
            break;
        case HSE_STATE_SAVE_BACKLOG:
            encoder->state = _st_save_backlog(encoder);
            break;
        case HSE_STATE_FLUSH_BITS:
            encoder->state = _st_flush_bit_buffer(encoder, &output);
            break;
        default:
            encoder->state = HSE_STATE_DONE;
            break;
        }

        if (output.len >= output.cap && encoder->state != HSE_STATE_DONE) {
            *written = output.len;
            return 1;
        }
    }

    *written = output.len;
    return 0;
}

static int _heatshrink_encoder_finish(heatshrink_encoder_t *encoder) {
    if (!encoder) return -1;
    encoder->finishing = 1;
    if (encoder->state == HSE_STATE_IDLE) encoder->state = HSE_STATE_SEARCH;
    return encoder->state != HSE_STATE_DONE;
}

void _DataCompress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    if (!out || !out_len) return;
    *out = NULL;
    *out_len = 0;
    if (!in && in_len != 0) return;
    if (in_len == 0) {
        *out = (unsigned char *)malloc(1);
        if (*out) (*out)[0] = 0;
        *out_len = *out ? 1 : 0;
        return;
    }

    size_t out_cap = in_len + (in_len >> 1) + 32u;
    unsigned char *buf = (unsigned char *)malloc(out_cap);
    if (!buf) return;

    memset(buf, 0, out_cap);

    _heatshrink_encoder_reset(&_hse);

    size_t input_off = 0;
    size_t output_off = 0;
    int finish_rc = 0;
    while (input_off < in_len || finish_rc != 0) {
        if (input_off < in_len) {
            size_t sunk = 0;
            if (_heatshrink_encoder_sink(&_hse, in + input_off, in_len - input_off, &sunk) < 0) {
                free(buf);
                _heatshrink_encoder_reset(&_hse);
                return;
            }
            input_off += sunk;
            if (input_off == in_len) {
                finish_rc = _heatshrink_encoder_finish(&_hse);
                if (finish_rc < 0) {
                    free(buf);
                    _heatshrink_encoder_reset(&_hse);
                    return;
                }
            }
        }

        do {
            if (output_off >= out_cap) {
                free(buf);
                _heatshrink_encoder_reset(&_hse);
                return;
            }

            size_t written = 0;
            int poll_rc = _heatshrink_encoder_poll(&_hse, buf + output_off, out_cap - output_off, &written);
            if (poll_rc < 0) {
                free(buf);
                _heatshrink_encoder_reset(&_hse);
                return;
            }
            output_off += written;

            if (poll_rc == 0) break;
        } while (1);

        if (input_off == in_len && _hse.state == HSE_STATE_DONE) break;
        if (input_off == in_len && finish_rc == 0) break;
    }

    unsigned char *shrunk = (unsigned char *)realloc(buf, output_off ? output_off : 1u);
    *out = shrunk ? shrunk : buf;
    *out_len = output_off;

    _heatshrink_encoder_reset(&_hse);
}

#ifdef TEST_COMPRESSION
void test_hse_reset(void) {
    _heatshrink_encoder_reset(&_hse);
}

int test_hse_sink(const unsigned char *data, size_t len, size_t *sunk) {
    return _heatshrink_encoder_sink(&_hse, data, len, sunk);
}

int test_hse_poll(unsigned char *out, size_t out_cap, size_t *written) {
    return _heatshrink_encoder_poll(&_hse, out, out_cap, written);
}

int test_hse_finish(void) {
    return _heatshrink_encoder_finish(&_hse);
}
#endif

/* Image ops */
void _Negative(unsigned char *buf, size_t len) { for (size_t i=0;i<len;i++) buf[i] = ~buf[i]; }

static void _swap_u8(unsigned char *a, unsigned char *b) {
    unsigned char tmp = *a;
    *a = *b;
    *b = tmp;
}

void _Mirror(unsigned char *buf, int width_bytes, int height) {
    if (!buf || width_bytes <= 0 || height <= 0) return;
    int width = width_bytes * 8;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            _swap_u8(&buf[y * width + x], &buf[y * width + (width - x - 1)]);
        }
    }
}

void _Rotate90(unsigned char *buf, int *width_bytes, int *height) {
    if (!buf || !width_bytes || !height || *width_bytes <= 0 || *height <= 0) return;
    int src_width = *width_bytes * 8;
    int src_height = *height;
    size_t total = (size_t)src_width * (size_t)src_height;
    unsigned char *tmp = (unsigned char *)malloc(total);
    if (!tmp) return;

    for (int y = 0; y < src_height; ++y) {
        for (int x = 0; x < src_width; ++x) {
            tmp[x * src_height + y] = buf[(src_height - y - 1) * src_width + x];
        }
    }

    memcpy(buf, tmp, total);
    free(tmp);
    *width_bytes = src_height / 8;
    *height = src_width;
}

void _Rotate180(unsigned char *buf, int width_bytes, int height) {
    if (!buf || width_bytes <= 0 || height <= 0) return;
    int width = width_bytes * 8;
    int total = width * height;
    for (int i = 0; i < total / 2; ++i) {
        _swap_u8(&buf[i], &buf[total - i - 1]);
    }
}

void _Rotate270(unsigned char *buf, int *width_bytes, int *height) {
    if (!buf || !width_bytes || !height || *width_bytes <= 0 || *height <= 0) return;
    int src_width = *width_bytes * 8;
    int src_height = *height;
    size_t total = (size_t)src_width * (size_t)src_height;
    unsigned char *tmp = (unsigned char *)malloc(total);
    if (!tmp) return;

    for (int y = 0; y < src_height; ++y) {
        for (int x = 0; x < src_width; ++x) {
            tmp[x * src_height + y] = buf[y * src_width + (src_width - x - 1)];
        }
    }

    memcpy(buf, tmp, total);
    free(tmp);
    *width_bytes = src_height / 8;
    *height = src_width;
}

int _GetSavePaperRightDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin) {
    if (!buf || width <= 0 || height <= 0) return width > 0 ? width - 1 : 0;
    for (int x = width - 1; x >= 0; --x) {
        for (int y = 0; y < height; ++y) {
            if (buf[y * width + x] != fill) {
                int edge = x + margin;
                return edge >= width ? width - 1 : edge;
            }
        }
    }
    return width - 1;
}

int _GetSavePaperDownDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin) {
    if (!buf || width <= 0 || height <= 0) return height > 0 ? height - 1 : 0;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            if (buf[y * width + x] != fill) {
                int edge = y + margin;
                return edge >= height ? height - 1 : edge;
            }
        }
    }
    return height - 1;
}

int _GetSavePaperUpDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin) {
    if (!buf || width <= 0 || height <= 0) return 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (buf[y * width + x] != fill) {
                int edge = y - margin;
                return edge < 0 ? 0 : edge;
            }
        }
    }
    return 0;
}

int _GetSavePaperLeftDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin) {
    if (!buf || width <= 0 || height <= 0) return 0;
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (buf[y * width + x] != fill) {
                int edge = x - margin;
                return edge < 0 ? 0 : edge;
            }
        }
    }
    return 0;
}

void _Resize(unsigned char *buf, int src_width, int src_height, int dst_width, int dst_height) {
    if (!buf || src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0) return;
    size_t total = (size_t)dst_width * (size_t)dst_height;
    unsigned char *tmp = (unsigned char *)malloc(total);
    if (!tmp) return;

    for (int y = 0; y < dst_height; ++y) {
        int sy = (y * src_height) / dst_height;
        for (int x = 0; x < dst_width; ++x) {
            int sx = (x * src_width) / dst_width;
            tmp[y * dst_width + x] = buf[sy * src_width + sx];
        }
    }

    memcpy(buf, tmp, total);
    free(tmp);
}

void _SavePaper(unsigned char *buf, int *width_bytes, int *height, unsigned char fill, unsigned int mask, int margin) {
    if (!buf || !width_bytes || !height || *width_bytes <= 0 || *height <= 0) return;
    int width = *width_bytes * 8;
    int top = 0;
    int left = 0;
    int right = width - 1;
    int bottom = *height - 1;

    if (mask & 1u) right = _GetSavePaperRightDots(buf, width, *height, fill, margin);
    if (mask & 2u) bottom = _GetSavePaperDownDots(buf, width, *height, fill, margin);
    if (mask & 4u) top = _GetSavePaperUpDots(buf, width, *height, fill, margin);
    if (mask & 8u) left = _GetSavePaperLeftDots(buf, width, *height, fill, margin);

    if (left == 0 && top == 0 && right == width - 1 && bottom == *height - 1) return;
    if (right < left || bottom < top) return;

    int new_width = (((right - left) + 1 + 7) / 8) * 8;
    int new_height = (((bottom - top) + 1 + 7) / 8) * 8;
    size_t total = (size_t)new_width * (size_t)new_height;
    unsigned char *tmp = (unsigned char *)malloc(total);
    if (!tmp) return;
    memset(tmp, fill, total);

    for (int y = top; y <= bottom && y < *height; ++y) {
        for (int x = left; x <= right && x < width; ++x) {
            tmp[(y - top) * new_width + (x - left)] = buf[y * width + x];
        }
    }

    memcpy(buf, tmp, total);
    free(tmp);
    *width_bytes = new_width / 8;
    *height = new_height;
}

/* Dither/gamma constants and implementation */

/* Bit-to-byte mask lookup (MSB-first bit ordering) */
static const unsigned char _bitToByte[8] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

/* Floyd 8x8 dispersed dither matrix */
static const unsigned char _Floyd8x8_disperse[64] = {
    0,  32, 8,  40, 2,  34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4,  36, 14, 46, 6,  38,
    60, 28, 52, 20, 62, 30, 54, 22,
    3,  35, 11, 43, 1,  33, 9,  41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7,  39, 13, 45, 5,  37,
    63, 31, 55, 23, 61, 29, 53, 21
};

/* Floyd 8x8 clustered dither matrix */
static const unsigned char _Floyd8x8_cluster[64] = {
    0,  16, 4,  20, 1,  17, 5,  21,
    24, 8,  28, 12, 25, 9,  29, 13,
    6,  22, 2,  18, 7,  23, 3,  19,
    30, 14, 26, 10, 31, 15, 27, 11,
    3,  19, 7,  23, 2,  18, 6,  22,
    27, 11, 31, 15, 26, 10, 30, 14,
    9,  25, 13, 29, 8,  24, 12, 28,
    21, 5,  17, 1,  20, 4,  16, 0
};

/* Floyd 16x16 ordered dither matrix */
static const unsigned char _Floyd16x16[256] = {
    0,   128, 32,  160, 8,   136, 40,  168, 2,   130, 34,  162, 10,  138, 42,  170,
    192, 64,  224, 96,  200, 72,  232, 104, 194, 66,  226, 98,  202, 74,  234, 106,
    48,  176, 16,  144, 56,  184, 24,  152, 50,  178, 18,  146, 58,  186, 26,  154,
    240, 112, 208, 80,  248, 120, 216, 88,  242, 114, 210, 82,  250, 122, 218, 90,
    12,  140, 44,  172, 4,   132, 36,  164, 14,  142, 46,  174, 6,   134, 38,  166,
    204, 76,  236, 108, 196, 68,  228, 100, 206, 78,  238, 110, 198, 70,  230, 102,
    60,  188, 28,  156, 52,  180, 20,  148, 62,  190, 30,  158, 54,  182, 22,  150,
    252, 124, 220, 92,  244, 116, 212, 84,  254, 126, 222, 94,  246, 118, 214, 86,
    3,   131, 35,  163, 11,  139, 43,  171, 1,   129, 33,  161, 9,   137, 41,  169,
    195, 67,  227, 99,  203, 75,  235, 107, 193, 65,  225, 97,  201, 73,  233, 105,
    51,  179, 19,  147, 59,  187, 27,  155, 49,  177, 17,  145, 57,  185, 25,  153,
    243, 115, 211, 83,  251, 123, 219, 91,  241, 113, 209, 81,  249, 121, 217, 89,
    15,  143, 47,  175, 7,   135, 39,  167, 13,  141, 45,  173, 5,   133, 37,  165,
    207, 79,  239, 111, 199, 71,  231, 103, 205, 77,  237, 109, 197, 69,  229, 101,
    63,  191, 31,  159, 55,  183, 23,  151, 61,  189, 29,  157, 53,  181, 21,  149,
    255, 127, 223, 95,  247, 119, 215, 87,  253, 125, 221, 93,  245, 117, 213, 85
};

/* Gamma lookup table (12 entries for luminance range [120, 250)) as IEEE 32-bit floats */
static const uint32_t _gamma_lut[12] = {
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3fe66666u,  /* 0.9 */
    0x3e4ccccd   /* 0.198 */
};

static unsigned char g_gamma[256];
static int g_gamma_ready = 0;
void _gamma_lut_init(float gamma);

static void _ensure_gamma_lut(void) {
    if (g_gamma_ready) return;
    _gamma_lut_init(1.0f);
    g_gamma_ready = 1;
}

void _gamma_lut_init(float gamma) {
    for (int i = 0; i < 256; ++i) {
        float v = (float)i / 255.0f;
        v = powf(v, gamma);
        int iv = (int)(v * 255.0f + 0.5f);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        g_gamma[i] = (unsigned char)iv;
    }
    g_gamma_ready = 1;
}

/* Calculate average luminance of image */
static float _calc_avg(const unsigned char *input, int width, int height) {
    if (!input || width <= 0 || height <= 0) return 0.0f;
    double sum = 0.0;
    int total = width * height;
    for (int i = 0; i < total; ++i) {
        sum += (double)input[i];
    }
    return (float)(sum / (double)total);
}

/* Lookup gamma correction factor based on average luminance */
static float _calc_gamma(float avg_luminance) {
    float result;
    if (avg_luminance < 120.0f) {
        memcpy(&result, &_gamma_lut[0], sizeof(float));  /* 0.9 */
        return result;
    }
    if (avg_luminance < 250.0f) {
        int idx = (int)((avg_luminance - 120.0f) / 10.0f);
        if (idx < 0) idx = 0;
        if (idx >= 12) idx = 11;
        memcpy(&result, &_gamma_lut[idx], sizeof(float));
        return result;
    }
    memcpy(&result, &_gamma_lut[11], sizeof(float));  /* 0.198 */
    return result;
}

/* Apply power transformation to linearize pixel values */
static void _calc_pow(float exponent, unsigned char *input, int width, int height) {
    if (!input || width <= 0 || height <= 0) return;
    if (isnan(exponent) || exponent == 1.0f) return;
    
    int total = width * height;
    for (int i = 0; i < total; ++i) {
        float normalized = (float)input[i] / 255.0f;
        float transformed = powf(normalized, exponent);
        int result = (int)(transformed * 255.0f + 0.5f);
        if (result < 0) result = 0;
        if (result > 255) result = 255;
        input[i] = (unsigned char)result;
    }
}

/* Safely update error buffer pixel with boundary checks */
static void _update_pixel(int32_t *error_buffer, int row, int col, int width, int height, int32_t error) {
    if (row < 0 || row >= height || col < 0 || col >= width) return;
    error_buffer[row * width + col] += error;
}

/* Core error diffusion with modified Floyd-Steinberg kernel */
static void _ErrorDiffuse(unsigned char *input, int width, int height, int32_t *error_buffer) {
    if (!input || !error_buffer || width <= 0 || height <= 0) return;
    
    /* Copy input to error buffer (promote to 32-bit) */
    for (int i = 0; i < width * height; ++i) {
        error_buffer[i] = (int32_t)input[i];
    }
    
    /* Scan and propagate error */
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int32_t pixel = error_buffer[row * width + col];
            int32_t error;
            
            if (pixel < 0x80) {
                error = pixel - 0;      /* Quantize to 0 */
                error_buffer[row * width + col] = 0;
            } else {
                error = pixel - 0xff;   /* Quantize to 255 */
                error_buffer[row * width + col] = 0xff;
            }
            
            /* Distribute error using modified kernel */
            _update_pixel(error_buffer, row, col + 1, width, height, (error * 5) / 32);
            _update_pixel(error_buffer, row, col + 2, width, height, (error * 3) / 32);
            _update_pixel(error_buffer, row + 1, col, width, height, (error * 3) / 32);
            _update_pixel(error_buffer, row + 1, col + 1, width, height, (error * 4) / 32);
            _update_pixel(error_buffer, row + 1, col + 2, width, height, (error * 2) / 32);
            _update_pixel(error_buffer, row + 1, col - 1, width, height, (error * 4) / 32);
            _update_pixel(error_buffer, row + 1, col - 2, width, height, (error * 2) / 32);
            _update_pixel(error_buffer, row + 2, col, width, height, (error * 3) / 32);
            _update_pixel(error_buffer, row + 2, col + 1, width, height, (error * 2) / 32);
            _update_pixel(error_buffer, row + 2, col - 1, width, height, (error * 2) / 32);
        }
    }
}

/* Error diffusion with gamma correction */
static void _BitmapErrorDiffuse(const unsigned char *input, int width, int height,
                                unsigned char *output, int output_width_bytes, unsigned char color_param) {
    if (!input || !output || width <= 0 || height <= 0) return;
    
    /* Allocate working buffers */
    unsigned char *linearized = (unsigned char *)malloc((size_t)width * (size_t)height);
    int32_t *error_buffer = (int32_t *)malloc((size_t)width * (size_t)height * sizeof(int32_t));
    
    if (!linearized || !error_buffer) {
        free(linearized);
        free(error_buffer);
        return;
    }
    
    /* Copy and linearize */
    memcpy(linearized, input, (size_t)width * (size_t)height);
    if (width > 0 && height > 0) {
        float avg = _calc_avg(linearized, width, height);
        float gamma = _calc_gamma(avg);
        float exponent = 1.0f / gamma;
        _calc_pow(exponent, linearized, width, height);
    }
    
    /* Apply error diffusion */
    _ErrorDiffuse(linearized, width, height, error_buffer);
    
    /* Quantize error buffer to bitmap */
    memset(output, 0, (size_t)output_width_bytes * (size_t)height);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int32_t error = error_buffer[row * width + col];
            int output_byte = row * output_width_bytes + col / 8;
            int bit_pos = col & 7;
            
            if ((error & 0xff) < 0x80) {
                if (color_param == 0xff) {
                    output[output_byte] &= (unsigned char)~_bitToByte[bit_pos];
                } else {
                    output[output_byte] |= _bitToByte[bit_pos];
                }
            }
        }
    }
    
    free(linearized);
    free(error_buffer);
}

/* Pack pixels into 1bpp buffer. in: pixel data (colors bytes per pixel), out: bytes buffer ((width+7)/8) */
void pack_line_to_1bpp(const unsigned char *in, unsigned char *out, unsigned width, unsigned colors, unsigned bitsPerColor) {
    _ensure_gamma_lut();
    unsigned out_bytes = (width + 7) / 8;
    unsigned bytes_per_sample = bitsPerColor > 8 ? 2u : 1u;
    unsigned pixel_stride = colors > 0 ? colors * bytes_per_sample : bytes_per_sample;
    memset(out, 0, out_bytes);
    for (unsigned x = 0; x < width; ++x) {
        unsigned r = 0, g = 0, b = 0;
        unsigned idx = x * pixel_stride;
        if (colors == 1) {
            r = in[idx]; g = in[idx]; b = in[idx];
        } else if (colors >= 3) {
            unsigned base = idx;
            r = in[base + 0]; g = in[base + 1]; b = in[base + 2];
        } else {
            r = in[idx]; g = in[idx]; b = in[idx];
        }

        /* Apply gamma table if present */
        unsigned lr = g_gamma[r];
        unsigned lg = g_gamma[g];
        unsigned lb = g_gamma[b];

        /* Luminance */
        unsigned lum = (299 * lr + 587 * lg + 114 * lb) / 1000;
        unsigned bit = lum < 128 ? 1 : 0; /* simple threshold */
        if (bit) out[x/8] |= (0x80 >> (x & 7));
    }
}



/* Logging */
static FILE *g_log = NULL;
static unsigned int g_page_index = 0;
int _InitLog(const char *path) { g_log = fopen(path?path:"/tmp/rw403b.log","w"); return g_log?0:-1; }
void _WriteLog(const char *msg) { if (g_log) fprintf(g_log, "%s\n", msg); }
void _CloseLog(void) { if (g_log) fclose(g_log); g_log = NULL; }

static void _WriteLogf(const char *fmt, ...) {
    if (!g_log || !fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
}

typedef struct rw403b_doc_s {
    int job_id;
    int copies;
    int speed;
    int darkness;
    int media_type;
    int gap_height_mm;
    int gap_offset_mm;
    int offset_mm;
    int x_mm;
    int y_mm;
    int width_mm;
    int height_mm;
    int print_pages;
    int rotate;
    int img_mirror;
    int img_negative;
    unsigned int save_paper_mask;
    int save_paper_margin;
    int dither_mode;
    int label_locate;
    int feed_bef_doc;
    int feed_aft_doc;
    int feed_bef_page;
    int feed_aft_page;
} rw403b_doc_t;

int _pdd_find_int(ppd_file_t *ppd, const char *name, int default_value) {
#ifdef HAVE_PPD
    if (!ppd || !name) return default_value;
    ppd_attr_t *a = ppdFindAttr(ppd, name, NULL);
    if (!a || !a->value) return default_value;
    return atoi(a->value);
#else
    (void)ppd;
    (void)name;
    return default_value;
#endif
}

void _set_pstops_with_ppd(rw403b_doc_t *doc) {
    if (!doc) return;
#ifdef HAVE_PPD
    const char *ppd_path = getenv("PPD");
    if (!ppd_path || !*ppd_path) return;

    ppd_file_t *ppd = ppdOpenFile(ppd_path);
    if (!ppd) {
        _WriteLogf("ppd open failed: %s", ppd_path);
        return;
    }

    doc->darkness = _pdd_find_int(ppd, "DefaultDarkness", doc->darkness);
    doc->speed = _pdd_find_int(ppd, "DefaultPrintSpeed", doc->speed * 10) / 10;
    doc->media_type = _pdd_find_int(ppd, "DefaultMediaType", doc->media_type);
    doc->x_mm = _pdd_find_int(ppd, "DefaultHorizontal", doc->x_mm);
    doc->y_mm = _pdd_find_int(ppd, "DefaultVertical", doc->y_mm);
    doc->dither_mode = _pdd_find_int(ppd, "DefaultPrintMode", doc->dither_mode);
    doc->gap_height_mm = _pdd_find_int(ppd, "DefaultGapHeight", doc->gap_height_mm);
    doc->gap_offset_mm = _pdd_find_int(ppd, "DefaultGapOffset", doc->gap_offset_mm);
    doc->rotate = _pdd_find_int(ppd, "DefaultRotate", doc->rotate);
    doc->img_mirror = _pdd_find_int(ppd, "DefaultImgMirror", doc->img_mirror);
    doc->img_negative = _pdd_find_int(ppd, "DefaultImgNegative", doc->img_negative);
    if (_pdd_find_int(ppd, "DefaultSavePaperLeft", (doc->save_paper_mask >> 3) & 1u)) doc->save_paper_mask |= 8u;
    if (_pdd_find_int(ppd, "DefaultSavePaperRight", doc->save_paper_mask & 1u)) doc->save_paper_mask |= 1u;
    if (_pdd_find_int(ppd, "DefaultSavePaperUp", (doc->save_paper_mask >> 2) & 1u)) doc->save_paper_mask |= 4u;
    if (_pdd_find_int(ppd, "DefaultSavePaperDown", (doc->save_paper_mask >> 1) & 1u)) doc->save_paper_mask |= 2u;
    doc->label_locate = _pdd_find_int(ppd, "DefaultLabelLocate", doc->label_locate);
    doc->feed_bef_doc = _pdd_find_int(ppd, "DefaultFeedBefDoc", doc->feed_bef_doc);
    doc->feed_aft_doc = _pdd_find_int(ppd, "DefaultFeedAftDoc", doc->feed_aft_doc);
    doc->feed_bef_page = _pdd_find_int(ppd, "DefaultFeedBefPage", doc->feed_bef_page);
    doc->feed_aft_page = _pdd_find_int(ppd, "DefaultFeedAftPage", doc->feed_aft_page);

    ppdClose(ppd);
#else
    (void)doc;
#endif
}

int _set_pstops_options(rw403b_doc_t *doc, int argc, char **argv, int num_options, cups_option_t *options) {
    if (!doc || !argv || argc < 6) return -1;

    memset(doc, 0, sizeof(*doc));
    doc->job_id = atoi(argv[1]);
    doc->copies = atoi(argv[4]);
    if (doc->copies <= 0) doc->copies = 1;
    doc->speed = 4;
    doc->darkness = 12;
    doc->media_type = 1;
    doc->gap_height_mm = 3;
    doc->gap_offset_mm = 0;
    doc->offset_mm = 0;
    doc->x_mm = 0;
    doc->y_mm = 0;
    doc->width_mm = 100;
    doc->height_mm = 150;
    doc->print_pages = 1;
    doc->rotate = 0;
    doc->img_mirror = 0;
    doc->img_negative = 0;
    doc->save_paper_mask = 0;
    doc->save_paper_margin = 24;
    doc->dither_mode = 0;
    doc->label_locate = 0;
    doc->feed_bef_doc = 0;
    doc->feed_aft_doc = 0;
    doc->feed_bef_page = 0;
    doc->feed_aft_page = 0;

    _set_pstops_with_ppd(doc);

    const char *v;
    v = cupsGetOption("Darkness", num_options, options);
    if (v) doc->darkness = atoi(v);

    v = cupsGetOption("PrintSpeed", num_options, options);
    if (v) {
        int raw = atoi(v);
        doc->speed = raw / 10;
        if (doc->speed <= 0) doc->speed = raw;
        if (doc->speed <= 0) doc->speed = 4;
    }

    v = cupsGetOption("MediaType", num_options, options);
    if (v) doc->media_type = atoi(v);

    v = cupsGetOption("GapHeight", num_options, options);
    if (v) doc->gap_height_mm = atoi(v);

    v = cupsGetOption("GapOffset", num_options, options);
    if (v) doc->gap_offset_mm = atoi(v);

    v = cupsGetOption("Feed", num_options, options);
    if (v) doc->offset_mm = atoi(v);

    v = cupsGetOption("Horizontal", num_options, options);
    if (v) doc->x_mm = atoi(v);

    v = cupsGetOption("Vertical", num_options, options);
    if (v) doc->y_mm = atoi(v);

    v = cupsGetOption("Rotate", num_options, options);
    if (v) doc->rotate = atoi(v);

    v = cupsGetOption("ImgMirror", num_options, options);
    if (v) doc->img_mirror = atoi(v);

    v = cupsGetOption("ImgNegative", num_options, options);
    if (v) doc->img_negative = atoi(v);

    v = cupsGetOption("PrintMode", num_options, options);
    if (v) doc->dither_mode = atoi(v);

    v = cupsGetOption("SavePaperLeft", num_options, options);
    if (v) {
        if (atoi(v)) doc->save_paper_mask |= 8u;
        else doc->save_paper_mask &= ~8u;
    }

    v = cupsGetOption("SavePaperRight", num_options, options);
    if (v) {
        if (atoi(v)) doc->save_paper_mask |= 1u;
        else doc->save_paper_mask &= ~1u;
    }

    v = cupsGetOption("SavePaperUp", num_options, options);
    if (v) {
        if (atoi(v)) doc->save_paper_mask |= 4u;
        else doc->save_paper_mask &= ~4u;
    }

    v = cupsGetOption("SavePaperDown", num_options, options);
    if (v) {
        if (atoi(v)) doc->save_paper_mask |= 2u;
        else doc->save_paper_mask &= ~2u;
    }

    v = cupsGetOption("LabelLocate", num_options, options);
    if (v) doc->label_locate = atoi(v);

    v = cupsGetOption("FeedBefDoc", num_options, options);
    if (v) doc->feed_bef_doc = atoi(v);

    v = cupsGetOption("FeedAftDoc", num_options, options);
    if (v) doc->feed_aft_doc = atoi(v);

    v = cupsGetOption("FeedBefPage", num_options, options);
    if (v) doc->feed_bef_page = atoi(v);

    v = cupsGetOption("FeedAftPage", num_options, options);
    if (v) doc->feed_aft_page = atoi(v);

    if (doc->darkness < 1) doc->darkness = 1;
    if (doc->darkness > 16) doc->darkness = 16;
    if (doc->speed < 1) doc->speed = 1;
    if (doc->speed > 8) doc->speed = 8;

    v = cupsGetOption("PageSize", num_options, options);
    if (v) {
        int w = 0, h = 0;
        if (sscanf(v, "w%dh%d", &w, &h) == 2 && w > 0 && h > 0) {
            doc->width_mm = (int)(w * 0.35277778f + 0.5f);
            doc->height_mm = (int)(h * 0.35277778f + 0.5f);
        }
    }

    _WriteLogf("opts job=%d copies=%d speed=%d darkness=%d page=%dx%dmm media=%d rotate=%d mirror=%d negative=%d save=0x%x x=%d y=%d mode=%d locate=%d fbd=%d fad=%d fbp=%d fap=%d",
               doc->job_id, doc->copies, doc->speed, doc->darkness,
               doc->width_mm, doc->height_mm, doc->media_type,
               doc->rotate, doc->img_mirror, doc->img_negative, doc->save_paper_mask,
               doc->x_mm, doc->y_mm, doc->dither_mode, doc->label_locate,
               doc->feed_bef_doc, doc->feed_aft_doc, doc->feed_bef_page, doc->feed_aft_page);
    return 0;
}

void _Gray2Bytes(const unsigned char *src, unsigned char *dst, int width_bytes, int height, int mode) {
    if (!src || !dst || width_bytes <= 0 || height <= 0) return;
    
    int width = width_bytes * 8;
    memset(dst, 0, (size_t)width_bytes * (size_t)height);
    
    /* Mode 4: Error diffusion with gamma correction */
    if (mode == 4) {
        _BitmapErrorDiffuse(src, width, height, dst, width_bytes, 0x00);
        return;
    }
    
    /* Modes 0-3: Threshold and ordered dithering */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char pixel = src[y * width + x];
            int byte_idx = y * width_bytes + x / 8;
            int bit_pos = x & 7;
            unsigned char threshold = 0x80;
            int quantize = 0;
            
            switch (mode) {
            case 0:
                /* Simple threshold at 0x81 */
                threshold = 0x81;
                quantize = pixel < threshold ? 1 : 0;
                break;
            case 1:
                /* Default threshold at 0xa1 */
                threshold = 0xa1;
                quantize = pixel < threshold ? 1 : 0;
                break;
            case 2:
                /* Floyd 8x8 dispersed */
                {
                    int thr = _Floyd8x8_disperse[(x & 7) + (y & 7) * 8];
                    quantize = (pixel >> 2) < thr ? 1 : 0;
                }
                break;
            case 3:
                /* Floyd 8x8 clustered */
                {
                    int thr = _Floyd8x8_cluster[(x & 7) + (y & 7) * 8];
                    quantize = (pixel >> 2) < thr ? 1 : 0;
                }
                break;
            default:
                /* Floyd 16x16 (fallback for other values) */
                {
                    int thr = _Floyd16x16[(y & 0xf) + (x & 0xf) * 0x10];
                    quantize = pixel < thr ? 1 : 0;
                }
                break;
            }
            
            if (quantize) {
                dst[byte_idx] |= _bitToByte[bit_pos];
            }
        }
    }
}

static void _UnpackBytesToPlane(const unsigned char *src, unsigned char *dst, int width_bytes, int height) {
    if (!src || !dst || width_bytes <= 0 || height <= 0) return;
    int width = width_bytes * 8;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char bit = src[y * width_bytes + x / 8] & (unsigned char)(0x80u >> (x & 7));
            dst[y * width + x] = bit ? 0x00 : 0xFF;
        }
    }
}

static void _ApplyDocImageOps(rw403b_doc_t *doc, unsigned char *plane, int *width_bytes, int *height) {
    if (!doc || !plane || !width_bytes || !height) return;

    int width = *width_bytes * 8;
    int total = width * *height;
    if (doc->img_negative) _Negative(plane, (size_t)total);
    if (doc->img_mirror) _Mirror(plane, *width_bytes, *height);

    switch (doc->rotate) {
    case 1:
        _Rotate180(plane, *width_bytes, *height);
        break;
    case 2:
        _Rotate90(plane, width_bytes, height);
        break;
    case 3:
        _Rotate270(plane, width_bytes, height);
        break;
    default:
        break;
    }

    if (doc->save_paper_mask) {
        _SavePaper(plane, width_bytes, height, 0xFF, doc->save_paper_mask, doc->save_paper_margin);
    }
}

void *_SetPaperOffset(const unsigned char *src, int width_bytes, int height,
                      unsigned char fill, int x_offset_bytes, int y_offset) {
    if (!src || width_bytes <= 0 || height <= 0) return NULL;

    size_t total = (size_t)width_bytes * (size_t)height;
    unsigned char *out = (unsigned char *)malloc(total);
    if (!out) return NULL;
    memset(out, fill, total);

    for (int y = 0; y < height; ++y) {
        int ny = y + y_offset;
        if (ny < 0 || ny >= height) continue;
        for (int xb = 0; xb < width_bytes; ++xb) {
            int nxb = xb + x_offset_bytes;
            if (nxb < 0 || nxb >= width_bytes) continue;
            out[(size_t)ny * (size_t)width_bytes + (size_t)nxb] = src[(size_t)y * (size_t)width_bytes + (size_t)xb];
        }
    }

    return out;
}

void _BitmapPrintCmdTSC(rw403b_doc_t *doc, unsigned char *bitmap, int width_bytes, int height, int copies) {
    if (!doc || !bitmap || width_bytes <= 0 || height <= 0) return;

    int total = width_bytes * height;
    unsigned char *comp = NULL;
    size_t comp_len = 0;
    _DataCompress(bitmap, (size_t)total, &comp, &comp_len);

    printf("SIZE %d mm,%d mm\r\n", doc->width_mm, doc->height_mm);
    if (doc->media_type == 0) {
        printf("GAP 0,0\r\n");
    } else if (doc->media_type == 2) {
        printf("BLINE %d mm,%d mm\r\n", doc->gap_height_mm, doc->gap_offset_mm);
    } else {
        printf("GAP %d mm,%d mm\r\n", doc->gap_height_mm, doc->gap_offset_mm);
    }
    printf("REFERENCE 0,0\r\n");
    printf("OFFSET %d mm\r\n", doc->offset_mm);
    printf("SETC AUTODOTTED OFF\r\n");
    printf("SETC PAUSEKEY OFF\r\n");
    printf("DENSITY %d\r\n", doc->darkness);
    printf("SPEED %d\r\n", doc->speed);
    printf("DIRECTION 0,0\r\n");
    printf("CLS\r\n");

    if (comp && comp_len > 0 && comp_len < (size_t)total) {
        printf("BITMAP %d,%d,%d,%d,3,%zu,", doc->x_mm, doc->y_mm, width_bytes, height, comp_len);
        fwrite(comp, 1, comp_len, stdout);
    } else {
        printf("BITMAP %d,%d,%d,%d,1,", doc->x_mm, doc->y_mm, width_bytes, height);
        fwrite(bitmap, 1, (size_t)total, stdout);
    }
    printf("\r\nPRINT 1,%d\r\n", copies > 0 ? copies : 1);
    fflush(stdout);

    free(comp);
}

void _DrvStartDoc(rw403b_doc_t *doc) {
    (void)doc;
    g_page_index = 0;
    _WriteLogf("DrvStartDoc++: page_index=%u", g_page_index);
}

void _DrvSendPage(rw403b_doc_t *doc, unsigned char *page, int width_bytes, int height, int copies) {
    if (!doc || !page) return;
    int total = width_bytes * height;
    for (int i = 0; i < total; ++i) page[i] ^= 0xFF;
    _BitmapPrintCmdTSC(doc, page, width_bytes, height, copies);
    g_page_index++;
    _WriteLogf("DrvSendPage: page_index=%u width_bytes=%d height=%d copies=%d",
               g_page_index, width_bytes, height, copies);
}

void _DrvEndDoc(rw403b_doc_t *doc) {
    (void)doc;
    _WriteLogf("DrvEndDoc--: total_pages=%u", g_page_index);
}

int _print_bitmap(rw403b_doc_t *doc, cups_file_t *input) {
    if (!doc || !input) return 1;

    cups_raster_t *r = cupsRasterOpen(cupsFileNumber(input), CUPS_RASTER_READ);
    if (!r) {
        _WriteLog("cupsRasterOpen failed");
        return 1;
    }

    _DrvStartDoc(doc);

    cups_page_header2_t h;
    unsigned page_index = 0;
    int rc = 0;
    while (cupsRasterReadHeader2(r, &h)) {
        page_index++;
        if (h.cupsWidth == 0 || h.cupsHeight == 0 || h.cupsBytesPerLine == 0) {
            _WriteLogf("skip invalid page header page=%u", page_index);
            continue;
        }

        if (h.cupsBitsPerColor != 8 && h.cupsBitsPerColor != 16) {
            _WriteLogf("unsupported bits/color=%u on page=%u", h.cupsBitsPerColor, page_index);
            continue;
        }

        size_t width_bytes_s = ((size_t)h.cupsWidth + 7u) / 8u;
        size_t height_s = (size_t)h.cupsHeight;
        if (width_bytes_s == 0 || height_s == 0 || width_bytes_s > SIZE_MAX / height_s) {
            _WriteLogf("page dimensions overflow page=%u width=%u height=%u", page_index, h.cupsWidth, h.cupsHeight);
            rc = 1;
            break;
        }

        int width_bytes = (int)width_bytes_s;
        int height = (int)((height_s + 7u) & ~7u);
        size_t page_size = width_bytes_s * (size_t)height;
        unsigned char *page = (unsigned char *)malloc(page_size);
        unsigned char *line = (unsigned char *)malloc(h.cupsBytesPerLine);
        if (!page || !line) {
            _WriteLogf("malloc failed page=%u page_bytes=%zu line_bytes=%u", page_index, page_size, h.cupsBytesPerLine);
            free(page);
            free(line);
            rc = 1;
            break;
        }

        memset(page, 0, page_size);
        unsigned rows_read = 0;
        for (unsigned y = 0; y < h.cupsHeight; ++y) {
            unsigned got = cupsRasterReadPixels(r, line, h.cupsBytesPerLine);
            if (got != h.cupsBytesPerLine) {
                _WriteLogf("short read page=%u row=%u got=%u expect=%u", page_index, y, got, h.cupsBytesPerLine);
                break;
            }
            pack_line_to_1bpp(line, page + (size_t)y * (size_t)width_bytes,
                              h.cupsWidth,
                              h.cupsNumColors ? h.cupsNumColors : 1,
                              h.cupsBitsPerColor);
            rows_read++;
        }

        if (rows_read != h.cupsHeight) {
            _WriteLogf("drop partial page=%u rows=%u/%u", page_index, rows_read, h.cupsHeight);
            free(page);
            free(line);
            continue;
        }

        size_t plane_size = (size_t)(width_bytes * 8) * (size_t)height;
        unsigned char *plane = (unsigned char *)malloc(plane_size);
        if (!plane) {
            free(page);
            free(line);
            rc = 1;
            break;
        }
        _UnpackBytesToPlane(page, plane, width_bytes, height);
        _ApplyDocImageOps(doc, plane, &width_bytes, &height);

        int plane_width = width_bytes * 8;
        int target_width = doc->width_mm > 0 ? doc->width_mm * 8 : 0;
        int target_height = doc->height_mm > 0 ? doc->height_mm * 8 : 0;
        if (target_width > 0 && target_height > 0 && (plane_width > target_width || height > target_height)) {
            int scaled_width;
            int scaled_height;
            if ((float)plane_width / (float)target_width >= (float)height / (float)target_height) {
                scaled_width = target_width;
                scaled_height = (int)((long long)height * (long long)scaled_width / (long long)plane_width);
            } else {
                scaled_height = target_height;
                scaled_width = (int)((long long)plane_width * (long long)scaled_height / (long long)height);
            }

            if (scaled_width < 8) scaled_width = 8;
            scaled_width = (scaled_width / 8) * 8;
            if (scaled_width < 8) scaled_width = 8;
            if (scaled_height < 1) scaled_height = 1;

            _Resize(plane, plane_width, height, scaled_width, scaled_height);
            plane_width = scaled_width;
            height = scaled_height;
            width_bytes = plane_width / 8;
        }

        int x_offset_bytes = doc->x_mm;
        if (target_width > 0 && plane_width < target_width) {
            size_t expanded_size = (size_t)target_width * (size_t)height;
            unsigned char *expanded = (unsigned char *)malloc(expanded_size);
            if (!expanded) {
                free(plane);
                free(page);
                free(line);
                rc = 1;
                break;
            }
            memset(expanded, 0xFF, expanded_size);

            int x_pad = (target_width - plane_width) / 2;
            for (int y = 0; y < height; ++y) {
                memcpy(expanded + (size_t)y * (size_t)target_width + (size_t)x_pad,
                       plane + (size_t)y * (size_t)plane_width,
                       (size_t)plane_width);
            }

            free(plane);
            plane = expanded;
            plane_width = target_width;
            width_bytes = plane_width / 8;
        }

        size_t packed_size = (size_t)width_bytes * (size_t)height;
        unsigned char *packed_page = (unsigned char *)malloc(packed_size);
        if (!packed_page) {
            free(plane);
            free(page);
            free(line);
            rc = 1;
            break;
        }
        _Gray2Bytes(plane, packed_page, width_bytes, height, doc->dither_mode);

        int y_offset_dots = doc->y_mm * 8;
        unsigned char *final_page = (unsigned char *)_SetPaperOffset(packed_page, width_bytes, height, 0x00,
                                          x_offset_bytes, y_offset_dots);
        if (!final_page) final_page = packed_page;

        _WriteLogf("send page=%u width_bytes=%d height=%d", page_index, width_bytes, height);
        _DrvSendPage(doc, final_page, width_bytes, height, doc->copies);

        if (final_page != packed_page) free(final_page);
        free(packed_page);
        free(plane);
        free(page);
        free(line);
    }

    _DrvEndDoc(doc);
    cupsRasterClose(r);
    return rc;
}

/* CUPS bindings (minimal) */
int cups_open_raster_from_stdin(cups_raster_t **raster, int *width, int *height, int *cupsBitsPerColor) {
    (void)raster; (void)width; (void)height; (void)cupsBitsPerColor;
    return -1;
}

#define PROGRAM_VERSION "0.1"

#ifndef TEST_COMPRESSION
static void print_usage(const char *prog) {
    printf("%s - Munbyn RW403B raster filter\n", prog);
    printf("Usage: %s [--help|-h] [--version] [--log PATH]\n", prog);
}

static void print_version(void) {
    printf("rastertorw403b %s\n", PROGRAM_VERSION);
}

int main(int argc, char **argv) {
    const char *logpath = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--version")) {
            print_version();
            return 0;
        }
        if (!strcmp(argv[i], "--log") && i+1 < argc) {
            logpath = argv[++i];
            continue;
        }
        /* Unknown args ignored for now */
    }

    if (_InitLog(logpath) != 0) {
        fprintf(stderr, "warning: cannot open log\n");
    }

    _WriteLog("rastertorw403b start");

    if (argc >= 6) {
        cups_file_t *in = NULL;
        if (argc >= 7) {
            in = cupsFileOpen(argv[6], "r");
        } else {
            in = cupsFileStdin();
        }

        int num_options = 0;
        cups_option_t *options = NULL;
        num_options = cupsParseOptions(argv[5], 0, &options);

        rw403b_doc_t doc;
        if (_set_pstops_options(&doc, argc, argv, num_options, options) == 0 && in) {
            _print_bitmap(&doc, in);
        } else {
            _WriteLog("invalid CUPS args/options or input file");
        }

        if (in) cupsFileClose(in);
        cupsFreeOptions(num_options, options);
    } else {
        /* Fallback behavior: read raster from stdin directly. */
        int in_fd = fileno(stdin);
        cups_raster_t *r = cupsRasterOpen(in_fd, CUPS_RASTER_READ);
        if (r) {
            cups_page_header2_t h;
            while (cupsRasterReadHeader2(r, &h)) {
                _WriteLogf("fallback page %ux%u bpl=%u", h.cupsWidth, h.cupsHeight, h.cupsBytesPerLine);
                unsigned char *line = (unsigned char *)malloc(h.cupsBytesPerLine);
                if (!line) break;
                for (unsigned y = 0; y < h.cupsHeight; ++y) {
                    if (cupsRasterReadPixels(r, line, h.cupsBytesPerLine) != h.cupsBytesPerLine) break;
                }
                free(line);
            }
            cupsRasterClose(r);
        }
    }

    _WriteLog("rastertorw403b end");
    _CloseLog();
    return 0;
}
#endif /* TEST_COMPRESSION */
