#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cups/cups.h>

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

int _set_pstops_options(rw403b_doc_t *doc, int argc, char **argv, int num_options, cups_option_t *options);

int main(void) {
    char *argv[] = {
        (char *)"rastertorw403b",
        (char *)"12",
        (char *)"user",
        (char *)"title",
        (char *)"2",
        (char *)"Rotate=2 ImgMirror=1 ImgNegative=1 SavePaperLeft=1 SavePaperDown=1 PrintMode=1 PageSize=w200h300 Horizontal=5 Vertical=7 LabelLocate=2 FeedBefDoc=3 FeedAftDoc=4 FeedBefPage=5 FeedAftPage=6",
        NULL
    };

    cups_option_t *options = NULL;
    int num_options = cupsParseOptions(argv[5], 0, &options);
    rw403b_doc_t doc;
    memset(&doc, 0, sizeof(doc));

    if (_set_pstops_options(&doc, 6, argv, num_options, options) != 0) {
        cupsFreeOptions(num_options, options);
        printf("FAIL: _set_pstops_options returned error\n");
        return 1;
    }

    cupsFreeOptions(num_options, options);

    if (doc.job_id != 12 || doc.copies != 2) {
        printf("FAIL: basic job parse\n");
        return 1;
    }
    if (doc.rotate != 2 || doc.img_mirror != 1 || doc.img_negative != 1) {
        printf("FAIL: transform flag parse\n");
        return 1;
    }
    if (doc.save_paper_mask != (8u | 2u)) {
        printf("FAIL: save paper mask parse got 0x%x\n", doc.save_paper_mask);
        return 1;
    }
    if (doc.dither_mode != 1) {
        printf("FAIL: print mode parse\n");
        return 1;
    }
    if (doc.x_mm != 5 || doc.y_mm != 7) {
        printf("FAIL: offset parse\n");
        return 1;
    }
    if (doc.width_mm <= 0 || doc.height_mm <= 0) {
        printf("FAIL: page size parse\n");
        return 1;
    }
    if (doc.label_locate != 2 || doc.feed_bef_doc != 3 || doc.feed_aft_doc != 4 ||
        doc.feed_bef_page != 5 || doc.feed_aft_page != 6) {
        printf("FAIL: feed/locate parse got locate=%d fbd=%d fad=%d fbp=%d fap=%d\n",
               doc.label_locate, doc.feed_bef_doc, doc.feed_aft_doc,
               doc.feed_bef_page, doc.feed_aft_page);
        return 1;
    }

    printf("PASS: option parse\n");
    return 0;
}
