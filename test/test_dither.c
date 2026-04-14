#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Declarations for functions in main implementation */
void _Gray2Bytes(const unsigned char *src, unsigned char *dst, int width_bytes, int height, int mode);

/* Test helper: create gradient image */
static void create_gradient(unsigned char *buffer, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            /* Create horizontal gradient from black (0) to white (255) */
            unsigned char value = (unsigned char)((x * 255) / width);
            buffer[y * width + x] = value;
        }
    }
}

/* Test helper: create uniform image */
static void create_uniform(unsigned char *buffer, int width, int height, unsigned char value) {
    memset(buffer, value, (size_t)width * (size_t)height);
}

/* Test helper: verify bitmap output has some variation (dither produces different bit patterns) */
static int check_dither_variation(const unsigned char *bitmap, int width_bytes, int height) {
    int ones = 0, zeros = 0;
    int total = width_bytes * height;
    for (int i = 0; i < total; ++i) {
        for (int b = 0; b < 8; ++b) {
            if (bitmap[i] & (0x80 >> b)) ones++;
            else zeros++;
        }
    }
    /* Check that we have both 0 and 1 bits (no uniform output) */
    return (ones > 0 && zeros > 0);
}

/* Test helper: count set bits in bitmap */
static int count_set_bits(const unsigned char *bitmap, int width_bytes, int height) {
    int count = 0;
    int total = width_bytes * height;
    for (int i = 0; i < total; ++i) {
        unsigned char byte = bitmap[i];
        for (int b = 0; b < 8; ++b) {
            if (byte & (0x80 >> b)) count++;
        }
    }
    return count;
}

int main(void) {
    printf("Running dithering tests\n");
    
    int width = 64;
    int height = 64;
    int width_bytes = (width + 7) / 8;
    
    unsigned char *grayscale = (unsigned char *)malloc((size_t)width * (size_t)height);
    unsigned char *output = (unsigned char *)malloc((size_t)width_bytes * (size_t)height);
    
    if (!grayscale || !output) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    
    /* Test 1: Mode 0 (simple threshold at 0x81) */
    {
        create_gradient(grayscale, width, height);
        memset(output, 0, (size_t)width_bytes * (size_t)height);
        _Gray2Bytes(grayscale, output, width_bytes, height, 0);
        
        int bits = count_set_bits(output, width_bytes, height);
        /* Gradient 0-255: pixels < 0x81 (129) are ~half, so bits should be ~half */
        int quarter = width * height / 4;
        if (bits >= quarter && bits <= 3 * quarter) {
            printf("PASS: Mode 0 (simple threshold) produces gradient output (%d/%d bits set)\n", bits, width * height);
        } else {
            printf("FAIL: Mode 0 produced unexpected bit pattern (%d bits, expected ~%d)\n", bits, width * height / 2);
            return 1;
        }
    }
    
    /* Test 2: Mode 1 (adjusted threshold at 0xa1) */
    {
        create_gradient(grayscale, width, height);
        memset(output, 0, (size_t)width_bytes * (size_t)height);
        _Gray2Bytes(grayscale, output, width_bytes, height, 1);
        
        int bits = count_set_bits(output, width_bytes, height);
        /* Gradient 0-255: pixels < 0xa1 (161) are ~63%, so bits should be ~60-70% */
        int target = (width * height * 161) / 256;
        int tolerance = width * height / 8;
        if (bits >= target - tolerance && bits <= target + tolerance) {
            printf("PASS: Mode 1 (adjusted threshold) produces gradient output (%d/%d bits set)\n", bits, width * height);
        } else {
            printf("FAIL: Mode 1 produced unexpected bit pattern (%d bits, expected ~%d)\n", bits, target);
            return 1;
        }
    }
    
    /* Test 3: Mode 2 (Floyd 8x8 dispersed) */
    {
        create_gradient(grayscale, width, height);
        memset(output, 0, (size_t)width_bytes * (size_t)height);
        _Gray2Bytes(grayscale, output, width_bytes, height, 2);
        
        if (check_dither_variation(output, width_bytes, height)) {
            printf("PASS: Mode 2 (Floyd 8x8 dispersed) produces dithered output\n");
        } else {
            printf("FAIL: Mode 2 produced uniform output\n");
            return 1;
        }
    }
    
    /* Test 4: Mode 3 (Floyd 8x8 cluster) */
    {
        create_gradient(grayscale, width, height);
        memset(output, 0, (size_t)width_bytes * (size_t)height);
        _Gray2Bytes(grayscale, output, width_bytes, height, 3);
        
        if (check_dither_variation(output, width_bytes, height)) {
            printf("PASS: Mode 3 (Floyd 8x8 clustered) produces dithered output\n");
        } else {
            printf("FAIL: Mode 3 produced uniform output\n");
            return 1;
        }
    }
    
    /* Test 5: Mode 4 (error diffusion with gamma) */
    {
        create_gradient(grayscale, width, height);
        memset(output, 0, (size_t)width_bytes * (size_t)height);
        _Gray2Bytes(grayscale, output, width_bytes, height, 4);
        
        if (check_dither_variation(output, width_bytes, height)) {
            printf("PASS: Mode 4 (error diffusion) produces output\n");
        } else {
            printf("FAIL: Mode 4 produced uniform output\n");
            return 1;
        }
    }
    
    /* Test 6: Uniform black (0x00) in all modes should produce mostly 1s (black)*/
    {
        create_uniform(grayscale, width, height, 0x00);
        for (int mode = 0; mode < 5; ++mode) {
            memset(output, 0x00, (size_t)width_bytes * (size_t)height);
            _Gray2Bytes(grayscale, output, width_bytes, height, mode);
            
            int bits = count_set_bits(output, width_bytes, height);
            if (bits >= width * height - width * height / 16) {  /* Allow some noise */
                printf("PASS: Mode %d with black input produces mostly 1s (%d/%d bits)\n", mode, bits, width * height);
            } else {
                printf("FAIL: Mode %d with black input produced too few 1s (%d bits, expected ~%d)\n", mode, bits, width * height);
                return 1;
            }
        }
    }
    
    /* Test 7: Uniform white (0xFF) in all modes should produce mostly 0s (white) */
    {
        create_uniform(grayscale, width, height, 0xFF);
        for (int mode = 0; mode < 5; ++mode) {
            memset(output, 0xff, (size_t)width_bytes * (size_t)height);
            _Gray2Bytes(grayscale, output, width_bytes, height, mode);
            
            int bits = count_set_bits(output, width_bytes, height);
            if (bits <= width * height / 16) {  /* Allow some noise */
                printf("PASS: Mode %d with white input produces mostly 0s (%d/%d bits)\n", mode, bits, width * height);
            } else {
                printf("FAIL: Mode %d with white input produced too many 1s (%d bits, expected ~0)\n", mode, bits);
                return 1;
            }
        }
    }
    
    /* Test 8: Mid-gray (~0xc0) should produce balanced output across modes */
    {
        create_uniform(grayscale, width, height, 0xc0);  /* Above all thresholds, but not pure white */
        for (int mode = 0; mode < 5; ++mode) {
            memset(output, 0x00, (size_t)width_bytes * (size_t)height);
            _Gray2Bytes(grayscale, output, width_bytes, height, mode);
            
            int bits = count_set_bits(output, width_bytes, height);
            int eighth = width * height / 8;
            /* For 0xc0, expect very few bits, but not necessarily zero due to dithering */
            if (bits <= 2 * eighth) {
                printf("PASS: Mode %d with 0xc0 input produces mostly 0s (%d/%d bits)\n", mode, bits, width * height);
            } else {
                printf("FAIL: Mode %d with 0xc0 input produced too many 1s (%d bits)\n", mode, bits);
                return 1;
            }
        }
    }
    
    free(grayscale);
    free(output);
    
    printf("\nAll dithering tests passed!\n");
    return 0;
}
