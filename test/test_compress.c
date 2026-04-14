#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TEST_BR_INDEX_BITS 11u
#define TEST_BR_LENGTH_BITS 4u
#define TEST_MIN_MATCH 3u
#define TEST_MAX_MATCH 16u

/* Declarations for functions in scaffold */
void _gamma_lut_init(float gamma);
void pack_line_to_1bpp(const unsigned char *in, unsigned char *out, unsigned width, unsigned colors, unsigned bitsPerColor);
void _DataCompress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);
void test_hse_reset(void);
int test_hse_sink(const unsigned char *data, size_t len, size_t *sunk);
int test_hse_poll(unsigned char *out, size_t out_cap, size_t *written);
int test_hse_finish(void);

typedef struct {
    int is_literal;
    unsigned value;
    unsigned length;
} token_t;

static int read_bits(const unsigned char *src, size_t src_len, size_t *bit_pos, unsigned count, unsigned *value) {
    unsigned out = 0;
    if (!src || !bit_pos || !value) return 0;
    for (unsigned i = 0; i < count; ++i) {
        size_t pos = *bit_pos + i;
        size_t byte_index = pos / 8u;
        unsigned bit_index = 7u - (unsigned)(pos % 8u);
        if (byte_index >= src_len) return 0;
        out = (out << 1u) | ((src[byte_index] >> bit_index) & 1u);
    }
    *bit_pos += count;
    *value = out;
    return 1;
}

static int decode_lz_bits(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_len) {
    size_t bit_pos = 0;
    size_t out_pos = 0;

    while (out_pos < dst_len) {
        unsigned tag = 0;
        if (!read_bits(src, src_len, &bit_pos, 1, &tag)) return 0;
        if (tag) {
            unsigned literal = 0;
            if (!read_bits(src, src_len, &bit_pos, 8, &literal)) return 0;
            dst[out_pos++] = (unsigned char)literal;
        } else {
            unsigned index_bits = 0;
            unsigned len_bits = 0;
            if (!read_bits(src, src_len, &bit_pos, TEST_BR_INDEX_BITS, &index_bits)) return 0;
            if (!read_bits(src, src_len, &bit_pos, TEST_BR_LENGTH_BITS, &len_bits)) return 0;

            size_t distance = (size_t)index_bits + 1u;
            size_t length = (size_t)len_bits + 1u;
            if (length < TEST_MIN_MATCH) return 0;
            if (distance == 0 || distance > out_pos) return 0;
            if (out_pos + length > dst_len) return 0;

            for (size_t i = 0; i < length; ++i) {
                dst[out_pos] = dst[out_pos - distance];
                out_pos++;
            }
        }
    }

    return 1;
}

static int decode_tokens(const unsigned char *src, size_t src_len, token_t *tokens, size_t *token_count, size_t max_tokens) {
    size_t bit_pos = 0;
    size_t count = 0;
    while (count < max_tokens) {
        size_t bits_left = src_len * 8u - bit_pos;
        if (bits_left < 1u) break;
        unsigned tag = 0;
        if (!read_bits(src, src_len, &bit_pos, 1, &tag)) break;
        if (tag) {
            unsigned literal = 0;
            if (!read_bits(src, src_len, &bit_pos, 8, &literal)) break;
            tokens[count].is_literal = 1;
            tokens[count].value = literal;
            tokens[count].length = 1;
        } else {
            unsigned index_bits = 0;
            unsigned len_bits = 0;
            if (!read_bits(src, src_len, &bit_pos, TEST_BR_INDEX_BITS, &index_bits)) break;
            if (!read_bits(src, src_len, &bit_pos, TEST_BR_LENGTH_BITS, &len_bits)) break;
            tokens[count].is_literal = 0;
            tokens[count].value = index_bits + 1u;
            tokens[count].length = len_bits + 1u;
        }
        count++;
    }
    *token_count = count;
    return 1;
}

static int all_tokens_literal(const token_t *tokens, size_t token_count) {
    for (size_t i = 0; i < token_count; ++i) {
        if (!tokens[i].is_literal) return 0;
    }
    return 1;
}

static int has_backref(const token_t *tokens, size_t token_count, unsigned distance, unsigned length) {
    for (size_t i = 0; i < token_count; ++i) {
        if (!tokens[i].is_literal && tokens[i].value == distance && tokens[i].length == length) {
            return 1;
        }
    }
    return 0;
}

static int compress_roundtrip_collect(const unsigned char *src, size_t src_len,
                                      unsigned char **comp, size_t *comp_len,
                                      token_t **tokens, size_t *token_count) {
    *comp = NULL;
    *comp_len = 0;
    *tokens = NULL;
    *token_count = 0;

    _DataCompress(src, src_len, comp, comp_len);
    if (!*comp || *comp_len == 0) return 0;

    unsigned char *decoded = (unsigned char *)malloc(src_len ? src_len : 1u);
    if (!decoded) return 0;
    if (!decode_lz_bits(*comp, *comp_len, decoded, src_len) || memcmp(decoded, src, src_len) != 0) {
        free(decoded);
        return 0;
    }
    free(decoded);

    *tokens = (token_t *)calloc(src_len ? src_len : 1u, sizeof(token_t));
    if (!*tokens) return 0;
    if (!decode_tokens(*comp, *comp_len, *tokens, token_count, src_len ? src_len : 1u)) {
        free(*tokens);
        *tokens = NULL;
        return 0;
    }

    return 1;
}

int main(void) {
    printf("Running compression/pack tests\n");

    /* Init gamma */
    _gamma_lut_init(1.0f);

    unsigned width = 32;
    unsigned colors = 3;
    unsigned bytes_per_pixel = colors;
    unsigned line_bytes = width * bytes_per_pixel;
    unsigned char *line = malloc(line_bytes);
    if (!line) return 2;

    /* Create test pattern: runs of black and white */
    for (unsigned x = 0; x < width; ++x) {
        if (x < 8 || (x >= 16 && x < 24)) {
            /* black */
            line[x*3+0] = 0; line[x*3+1] = 0; line[x*3+2] = 0;
        } else {
            /* white */
            line[x*3+0] = 255; line[x*3+1] = 255; line[x*3+2] = 255;
        }
    }

    unsigned out_bytes = (width + 7) / 8;
    unsigned char *out_line = malloc(out_bytes);
    if (!out_line) { free(line); return 2; }

    pack_line_to_1bpp(line, out_line, width, colors, 8);

    /* Basic check: out_line should have bytes set */
    int bits_set = 0;
    for (unsigned i = 0; i < out_bytes; ++i) bits_set += out_line[i] != 0;
    if (!bits_set) {
        printf("FAIL: packed line empty\n");
        free(line); free(out_line); return 1;
    }

    /* Compress packed line */
    unsigned char *comp = NULL; size_t comp_len = 0;
    _DataCompress(out_line, out_bytes, &comp, &comp_len);
    if (!comp || comp_len == 0) {
        printf("FAIL: compression produced empty output\n");
        free(line); free(out_line); if (comp) free(comp); return 1;
    }

    unsigned char *decoded = malloc(out_bytes);
    if (!decoded) {
        printf("FAIL: decoded alloc\n");
        free(line); free(out_line); free(comp); return 1;
    }

    if (!decode_lz_bits(comp, comp_len, decoded, out_bytes)) {
        printf("FAIL: decode roundtrip failed\n");
        free(line); free(out_line); free(comp); free(decoded); return 1;
    }

    if (memcmp(decoded, out_line, out_bytes) != 0) {
        printf("FAIL: decoded bytes mismatch\n");
        free(line); free(out_line); free(comp); free(decoded); return 1;
    }

    unsigned char repeated[256];
    memset(repeated, 0xAA, sizeof(repeated));
    unsigned char *comp_repeat = NULL;
    size_t comp_repeat_len = 0;
    _DataCompress(repeated, sizeof(repeated), &comp_repeat, &comp_repeat_len);
    if (!comp_repeat || comp_repeat_len == 0 || comp_repeat_len >= sizeof(repeated)) {
        printf("FAIL: repeated data did not compress\n");
        free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
    }

    {
        const unsigned char mixed[] = "ABCABC";
        unsigned char *comp_mixed = NULL;
        size_t comp_mixed_len = 0;
        _DataCompress(mixed, sizeof(mixed) - 1u, &comp_mixed, &comp_mixed_len);
        if (!comp_mixed || comp_mixed_len == 0) {
            printf("FAIL: mixed vector compress\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_mixed); return 1;
        }

        token_t tokens[8];
        size_t token_count = 0;
        if (!decode_tokens(comp_mixed, comp_mixed_len, tokens, &token_count, 8)) {
            printf("FAIL: mixed token decode\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_mixed); return 1;
        }

        if (token_count < 4 ||
            !tokens[0].is_literal || tokens[0].value != 'A' ||
            !tokens[1].is_literal || tokens[1].value != 'B' ||
            !tokens[2].is_literal || tokens[2].value != 'C' ||
            tokens[3].is_literal || tokens[3].value != 3u || tokens[3].length != 3u) {
            printf("FAIL: mixed vector token pattern mismatch\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_mixed); return 1;
        }

        unsigned char mixed_decoded[sizeof(mixed) - 1u];
        if (!decode_lz_bits(comp_mixed, comp_mixed_len, mixed_decoded, sizeof(mixed) - 1u) ||
            memcmp(mixed_decoded, mixed, sizeof(mixed) - 1u) != 0) {
            printf("FAIL: mixed vector roundtrip\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_mixed); return 1;
        }

        free(comp_mixed);
    }

    {
        const unsigned char stream_src[] = "ABCDABCDABCDABCDABCDABCD";
        unsigned char out_stream[256];
        size_t out_len = 0;
        test_hse_reset();

        size_t sunk = 0;
        if (test_hse_sink(stream_src, 5, &sunk) < 0 || sunk != 5u) {
            printf("FAIL: stream sink chunk 1\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
        }
        if (test_hse_sink(stream_src + 5, sizeof(stream_src) - 1u - 5u, &sunk) < 0 || sunk != sizeof(stream_src) - 1u - 5u) {
            printf("FAIL: stream sink chunk 2\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
        }
        if (test_hse_finish() < 0) {
            printf("FAIL: stream finish\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
        }

        for (;;) {
            size_t written = 0;
            int rc = test_hse_poll(out_stream + out_len, 3, &written);
            out_len += written;
            if (rc < 0) {
                printf("FAIL: stream poll\n");
                free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
            }
            if (rc == 0) break;
            if (out_len + 3 > sizeof(out_stream)) {
                printf("FAIL: stream output overflow\n");
                free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
            }
        }

        unsigned char stream_decoded[sizeof(stream_src) - 1u];
        if (!decode_lz_bits(out_stream, out_len, stream_decoded, sizeof(stream_src) - 1u) ||
            memcmp(stream_decoded, stream_src, sizeof(stream_src) - 1u) != 0) {
            printf("FAIL: stream roundtrip\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
        }
    }

    {
        const unsigned char len2_src[] = "ABAB";
        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(len2_src, sizeof(len2_src) - 1u, &comp_case, &comp_case_len, &tokens, &token_count) ||
            !all_tokens_literal(tokens, token_count)) {
            printf("FAIL: len2 threshold expected literal-only encoding\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    {
        const unsigned char len3_src[] = "ABCABC";
        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(len3_src, sizeof(len3_src) - 1u, &comp_case, &comp_case_len, &tokens, &token_count) ||
            !has_backref(tokens, token_count, 3u, 3u)) {
            printf("FAIL: len3 threshold expected distance=3 length=3 backref\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    {
        const unsigned char len16_src[] = "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP";
        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(len16_src, sizeof(len16_src) - 1u, &comp_case, &comp_case_len, &tokens, &token_count) ||
            !has_backref(tokens, token_count, 16u, 16u)) {
            printf("FAIL: len16 threshold expected distance=16 length=16 backref\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    {
        const unsigned char len17_src[] = "ABCDEFGHIJKLMNOPQABCDEFGHIJKLMNOPQ";
        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(len17_src, sizeof(len17_src) - 1u, &comp_case, &comp_case_len, &tokens, &token_count) ||
            !has_backref(tokens, token_count, 17u, 16u) ||
            token_count < 2 || !tokens[token_count - 1].is_literal || tokens[token_count - 1].value != 'Q') {
            printf("FAIL: len17 threshold expected capped backref plus trailing literal\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    {
        unsigned char dist1_src[17];
        memset(dist1_src, 'Z', sizeof(dist1_src));
        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(dist1_src, sizeof(dist1_src), &comp_case, &comp_case_len, &tokens, &token_count) ||
            !has_backref(tokens, token_count, 1u, 16u)) {
            printf("FAIL: distance1 expected length=16 backref\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    {
        unsigned char dist2048_src[2048 + 16];
        uint32_t state = 0x12345678u;
        for (size_t i = 0; i < 2048; ++i) {
            state = state * 1664525u + 1013904223u;
            dist2048_src[i] = (unsigned char)(state >> 24);
        }
        for (size_t off = 1; off + 16u <= 2048u; ++off) {
            if (memcmp(dist2048_src, dist2048_src + off, 16u) == 0) {
                printf("FAIL: distance2048 source not unique enough\n");
                free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); return 1;
            }
        }
        memcpy(dist2048_src + 2048u, dist2048_src, 16u);

        unsigned char *comp_case = NULL;
        token_t *tokens = NULL;
        size_t comp_case_len = 0;
        size_t token_count = 0;
        if (!compress_roundtrip_collect(dist2048_src, sizeof(dist2048_src), &comp_case, &comp_case_len, &tokens, &token_count) ||
            !has_backref(tokens, token_count, 2048u, 16u)) {
            printf("FAIL: distance2048 expected length=16 backref\n");
            free(line); free(out_line); free(comp); free(decoded); free(comp_repeat); free(comp_case); free(tokens); return 1;
        }
        free(comp_case);
        free(tokens);
    }

    printf("PASS: packed %u bytes -> compressed %zu bytes, repeated 256 -> %zu\n", out_bytes, comp_len, comp_repeat_len);

    free(line); free(out_line); free(comp); free(decoded); free(comp_repeat);
    return 0;
}
