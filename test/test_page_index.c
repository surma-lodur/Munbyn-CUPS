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

int _InitLog(const char *path);
void _CloseLog(void);
void _DrvStartDoc(rw403b_doc_t *doc);
void _DrvSendPage(rw403b_doc_t *doc, unsigned char *page, int width_bytes, int height, int copies);
void _DrvEndDoc(rw403b_doc_t *doc);

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

int main(void) {
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/rw403b_pageidx_%ld.log", (long)getpid());

    if (_InitLog(log_path) != 0) {
        printf("FAIL: init log\n");
        return 1;
    }

    rw403b_doc_t doc;
    memset(&doc, 0, sizeof(doc));
    doc.width_mm = 100;
    doc.height_mm = 150;
    doc.media_type = 1;
    doc.gap_height_mm = 3;
    doc.darkness = 180;
    doc.speed = 4;

    unsigned char page[8] = {0};

    FILE *tmp = tmpfile();
    if (!tmp) {
        _CloseLog();
        printf("FAIL: tmpfile\n");
        return 1;
    }

    int saved_stdout = dup(fileno(stdout));
    if (saved_stdout < 0 || dup2(fileno(tmp), fileno(stdout)) < 0) {
        if (saved_stdout >= 0) close(saved_stdout);
        fclose(tmp);
        _CloseLog();
        printf("FAIL: redirect stdout\n");
        return 1;
    }

    _DrvStartDoc(&doc);
    _DrvSendPage(&doc, page, 2, 4, 1);
    _DrvSendPage(&doc, page, 2, 4, 1);
    _DrvSendPage(&doc, page, 2, 4, 1);
    _DrvEndDoc(&doc);
    fflush(stdout);

    dup2(saved_stdout, fileno(stdout));
    close(saved_stdout);
    fclose(tmp);
    _CloseLog();

    int ok = 1;
    ok &= file_contains(log_path, "DrvStartDoc++: page_index=0");
    ok &= file_contains(log_path, "DrvSendPage: page_index=1");
    ok &= file_contains(log_path, "DrvSendPage: page_index=2");
    ok &= file_contains(log_path, "DrvSendPage: page_index=3");
    ok &= file_contains(log_path, "DrvEndDoc--: total_pages=3");

    unlink(log_path);

    if (!ok) {
        printf("FAIL: page index logs\n");
        return 1;
    }

    printf("PASS: page index logs\n");
    return 0;
}
