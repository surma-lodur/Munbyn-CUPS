#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void _Mirror(unsigned char *buf, int width_bytes, int height);
void _Rotate90(unsigned char *buf, int *width_bytes, int *height);
void _Rotate180(unsigned char *buf, int width_bytes, int height);
void _Rotate270(unsigned char *buf, int *width_bytes, int *height);
void _Resize(unsigned char *buf, int src_width, int src_height, int dst_width, int dst_height);
void _SavePaper(unsigned char *buf, int *width_bytes, int *height, unsigned char fill, unsigned int mask, int margin);
int _GetSavePaperRightDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin);
int _GetSavePaperDownDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin);
int _GetSavePaperUpDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin);
int _GetSavePaperLeftDots(const unsigned char *buf, int width, int height, unsigned char fill, int margin);
void *_SetPaperOffset(const unsigned char *src, int width_bytes, int height,
                      unsigned char fill, int x_offset_bytes, int y_offset);

static int expect_equal(const unsigned char *got, const unsigned char *want, size_t n, const char *label) {
    if (memcmp(got, want, n) == 0) return 1;
    printf("FAIL: %s mismatch\n", label);
    return 0;
}

int main(void) {
    {
        unsigned char row8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        unsigned char want[8] = {7, 6, 5, 4, 3, 2, 1, 0};
        _Mirror(row8, 1, 1);
        if (!expect_equal(row8, want, 8, "mirror")) return 1;
    }

    {
        unsigned char buf[64];
        unsigned char want90[64] = {
            56,48,40,32,24,16,8,0,
            57,49,41,33,25,17,9,1,
            58,50,42,34,26,18,10,2,
            59,51,43,35,27,19,11,3,
            60,52,44,36,28,20,12,4,
            61,53,45,37,29,21,13,5,
            62,54,46,38,30,22,14,6,
            63,55,47,39,31,23,15,7
        };
        for (int i = 0; i < 64; ++i) buf[i] = (unsigned char)i;
        int width_bytes = 1;
        int height = 8;
        _Rotate90(buf, &width_bytes, &height);
        if (width_bytes != 1 || height != 8 || !expect_equal(buf, want90, 64, "rotate90")) return 1;

        _Rotate270(buf, &width_bytes, &height);
        for (int i = 0; i < 64; ++i) {
            if (buf[i] != (unsigned char)i) {
                printf("FAIL: rotate270 inverse mismatch\n");
                return 1;
            }
        }
    }

    {
        unsigned char buf[64];
        unsigned char want180[64];
        for (int i = 0; i < 64; ++i) {
            buf[i] = (unsigned char)i;
            want180[i] = (unsigned char)(63 - i);
        }
        _Rotate180(buf, 1, 8);
        if (!expect_equal(buf, want180, 64, "rotate180")) return 1;
    }

    {
        unsigned char buf[16] = {
            0, 1, 2, 3,
            4, 5, 6, 7,
            8, 9, 10, 11,
            12, 13, 14, 15
        };
        unsigned char want[4] = {0, 2, 8, 10};
        _Resize(buf, 4, 4, 2, 2);
        if (!expect_equal(buf, want, 4, "resize")) return 1;
    }

    {
        unsigned char buf[16 * 16];
        memset(buf, 0xFF, sizeof(buf));
        for (int y = 5; y <= 8; ++y) {
            for (int x = 6; x <= 9; ++x) {
                buf[y * 16 + x] = 0x00;
            }
        }

        if (_GetSavePaperLeftDots(buf, 16, 16, 0xFF, 0) != 6) {
            printf("FAIL: savepaper left\n");
            return 1;
        }
        if (_GetSavePaperRightDots(buf, 16, 16, 0xFF, 0) != 9) {
            printf("FAIL: savepaper right\n");
            return 1;
        }
        if (_GetSavePaperUpDots(buf, 16, 16, 0xFF, 0) != 5) {
            printf("FAIL: savepaper up\n");
            return 1;
        }
        if (_GetSavePaperDownDots(buf, 16, 16, 0xFF, 0) != 8) {
            printf("FAIL: savepaper down\n");
            return 1;
        }

        int width_bytes = 2;
        int height = 16;
        _SavePaper(buf, &width_bytes, &height, 0xFF, 0x0F, 0);
        if (width_bytes != 1 || height != 8) {
            printf("FAIL: savepaper dimensions got %d x %d\n", width_bytes, height);
            return 1;
        }
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                unsigned char want = (y < 4 && x < 4) ? 0x00 : 0xFF;
                if (buf[y * 8 + x] != want) {
                    printf("FAIL: savepaper crop content\n");
                    return 1;
                }
            }
        }
    }

    {
        unsigned char src[8] = {
            1, 2,
            3, 4,
            5, 6,
            7, 8
        };
        unsigned char want[8] = {
            0xFF, 0xFF,
            1,    2,
            3,    4,
            5,    6
        };
        unsigned char *off = (unsigned char *)_SetPaperOffset(src, 2, 4, 0xFF, 0, 1);
        if (!off) {
            printf("FAIL: setpaperoffset alloc\n");
            return 1;
        }
        if (!expect_equal(off, want, sizeof(want), "setpaperoffset")) {
            free(off);
            return 1;
        }
        free(off);
    }

    printf("PASS: image ops\n");
    return 0;
}
