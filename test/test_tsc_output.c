#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Keep in sync with linux/rastertorw403b.c for ABI-compatible call. */
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

void _BitmapPrintCmdTSC(rw403b_doc_t *doc, unsigned char *bitmap, int width_bytes, int height, int copies);

static int mem_contains(const unsigned char *buf, size_t n, const char *needle) {
    size_t m = strlen(needle);
    if (m == 0 || n < m) return 0;
    for (size_t i = 0; i + m <= n; ++i) {
        if (memcmp(buf + i, needle, m) == 0) return 1;
    }
    return 0;
}

int main(void) {
    rw403b_doc_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.width_mm = 100;
    doc.height_mm = 150;
    doc.media_type = 1;
    doc.gap_height_mm = 3;
    doc.gap_offset_mm = 0;
    doc.darkness = 180;
    doc.speed = 4;
    doc.copies = 1;

    unsigned char bitmap[8] = {0xFF, 0x00, 0xF0, 0x0F, 0xAA, 0x55, 0x33, 0xCC};

    FILE *tmp = tmpfile();
    if (!tmp) {
        printf("FAIL: tmpfile\n");
        return 1;
    }

    int saved_stdout = dup(fileno(stdout));
    if (saved_stdout < 0) {
        fclose(tmp);
        printf("FAIL: dup stdout\n");
        return 1;
    }

    if (dup2(fileno(tmp), fileno(stdout)) < 0) {
        close(saved_stdout);
        fclose(tmp);
        printf("FAIL: dup2 tmp->stdout\n");
        return 1;
    }

    _BitmapPrintCmdTSC(&doc, bitmap, 2, 4, 1);
    fflush(stdout);

    if (dup2(saved_stdout, fileno(stdout)) < 0) {
        close(saved_stdout);
        fclose(tmp);
        printf("FAIL: restore stdout\n");
        return 1;
    }
    close(saved_stdout);

    rewind(tmp);

    unsigned char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf), tmp);
    int ok = 1;
    ok &= mem_contains(buf, n, "SIZE 100 mm,150 mm");
    ok &= mem_contains(buf, n, "GAP 3 mm,0 mm");
    ok &= mem_contains(buf, n, "SETC AUTODOTTED OFF");
    ok &= mem_contains(buf, n, "SETC PAUSEKEY OFF");
    ok &= mem_contains(buf, n, "BITMAP 0,0,2,4,");
    ok &= mem_contains(buf, n, "PRINT 1,1");

    fclose(tmp);

    if (!ok) {
        printf("FAIL: missing expected TSC framing tokens\n");
        return 1;
    }

    printf("PASS: TSC framing tokens present\n");
    return 0;
}
