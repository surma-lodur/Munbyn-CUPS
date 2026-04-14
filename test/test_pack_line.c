#include <stdio.h>
#include <string.h>

void pack_line_to_1bpp(const unsigned char *in, unsigned char *out, unsigned width, unsigned colors, unsigned bitsPerColor);

int main(void) {
    unsigned char in[8] = { 0, 64, 127, 128, 129, 200, 255, 30 };
    unsigned char out[1] = { 0 };

    pack_line_to_1bpp(in, out, 8, 1, 8);

    if (out[0] != 0xE1) {
        printf("FAIL: expected 0xE1 got 0x%02X\n", out[0]);
        return 1;
    }

    printf("PASS: pack line gamma init\n");
    return 0;
}
