CC=gcc
PKG_CFLAGS=$(shell pkg-config --cflags cups 2>/dev/null || echo "")
PKG_LIBS=$(shell pkg-config --libs cups 2>/dev/null || echo "-lcups")
CFLAGS=-O2 -Wall -DHAVE_PPD $(PKG_CFLAGS)
LDFLAGS=$(PKG_LIBS) -lm
BUILD_DIR=build
TARGET=$(BUILD_DIR)/rastertorw403b
FILTER_NAME=rastertorw403b
PPD_NAME=RW403B.ppd
CUPS_FILTER_DIR?=$(shell if [ -d /usr/lib/cups/filter ]; then echo /usr/lib/cups/filter; elif [ -d /usr/libexec/cups/filter ]; then echo /usr/libexec/cups/filter; else echo /usr/lib/cups/filter; fi)
CUPS_MODEL_DIR?=/usr/share/cups/model

all: $(TARGET)

rastertorw403b: $(TARGET)

$(TARGET): linux/rastertorw403b.c | build_dir
	$(CC) $(CFLAGS) -o $@ linux/rastertorw403b.c $(LDFLAGS)

test: test/test_compress test/test_tsc_output test/test_image_ops test/test_options test/test_dither test/test_pack_line test/test_page_index
	./test/test_compress
	./test/test_tsc_output
	./test/test_image_ops
	./test/test_options
	./test/test_dither
	./test/test_pack_line
	./test/test_page_index

test/test_compress: test/test_compress.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_compress.c linux/rastertorw403b.c $(LDFLAGS)

test/test_tsc_output: test/test_tsc_output.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_tsc_output.c linux/rastertorw403b.c $(LDFLAGS)

test/test_image_ops: test/test_image_ops.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_image_ops.c linux/rastertorw403b.c $(LDFLAGS)

test/test_options: test/test_options.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_options.c linux/rastertorw403b.c $(LDFLAGS)

test/test_dither: test/test_dither.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_dither.c linux/rastertorw403b.c $(LDFLAGS)

test/test_pack_line: test/test_pack_line.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_pack_line.c linux/rastertorw403b.c $(LDFLAGS)

test/test_page_index: test/test_page_index.c linux/rastertorw403b.c | test_dir
	$(CC) $(CFLAGS) -DTEST_COMPRESSION -o $@ test/test_page_index.c linux/rastertorw403b.c $(LDFLAGS)

test_dir:
	mkdir -p test

build_dir:
	mkdir -p $(BUILD_DIR)

install: $(TARGET)
	install -d $(DESTDIR)$(CUPS_FILTER_DIR)
	install -m 755 $(TARGET) $(DESTDIR)$(CUPS_FILTER_DIR)/$(FILTER_NAME)
	install -d $(DESTDIR)$(CUPS_MODEL_DIR)/Munbyn
	install -m 644 ppd/$(PPD_NAME) $(DESTDIR)$(CUPS_MODEL_DIR)/Munbyn/$(PPD_NAME)

clean:
	rm -f $(TARGET) test/test_compress test/test_tsc_output test/test_image_ops test/test_options test/test_dither test/test_pack_line test/test_page_index

.PHONY: all clean test test_dir build_dir install
