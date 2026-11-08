# Host build. The Amiga window is src/amiga.c and links GFOpenLayout on OS 3.2.
CC ?= gcc
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
BUILD = build
LAYOUT ?= ../gfopenlayout

.PHONY: all test preview clean

all: $(BUILD)/gfopenslicer $(BUILD)/test_slice

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/gfopenslicer: src/cli.c src/stl.c src/slice.c include/gs.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ src/cli.c src/stl.c src/slice.c -lm

$(BUILD)/test_slice: tests/test_slice.c src/stl.c src/slice.c include/gs.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ tests/test_slice.c src/stl.c src/slice.c -lm

$(BUILD)/preview: tools/preview.c src/job.c src/stl.c src/slice.c src/markup.h include/gs.h include/gs_job.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Isrc -I$(LAYOUT)/include -o $@ tools/preview.c src/job.c src/stl.c src/slice.c \
	    $(LAYOUT)/ui/olui.c $(LAYOUT)/ui/olui_css.c $(LAYOUT)/ui/olui_markup.c $(LAYOUT)/ui/olui_theme_none.c \
	    $(LAYOUT)/build/libopenlayout.a -lm

test: $(BUILD)/test_slice
	$(BUILD)/test_slice

preview: $(BUILD)/preview

clean:
	rm -rf $(BUILD)
