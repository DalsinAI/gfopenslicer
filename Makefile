# Host build. The Amiga window is src/amiga.c and links GFOpenLayout on OS 3.2.
# src/make.c is the solid editor: a cube with a hole, the same window.
CC ?= gcc
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
BUILD = build
SDK ?= /tmp/sdk/Include
LAYOUT ?= ../gfopenlayout

.PHONY: all test preview clean

all: $(BUILD)/gfopenslicer $(BUILD)/test_slice $(BUILD)/test_model $(BUILD)/test_gpu $(BUILD)/test_edit

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/gfopenslicer: src/cli.c src/stl.c src/slice.c include/gs.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ src/cli.c src/stl.c src/slice.c -lm

$(BUILD)/test_slice: tests/test_slice.c src/stl.c src/slice.c src/view.c src/solid.c include/gs.h include/gs_view.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ tests/test_slice.c src/stl.c src/slice.c src/view.c src/solid.c -lm

$(BUILD)/test_model: tests/test_model.c src/model.c src/stl.c src/view.c src/solid.c include/gs_model.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ tests/test_model.c src/model.c src/stl.c src/view.c src/solid.c -lm

$(BUILD)/test_gpu: tests/test_gpu.c src/gpu.c src/model.c src/stl.c src/view.c src/solid.c include/gs_gpu.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -I$(SDK) -o $@ tests/test_gpu.c src/gpu.c src/model.c src/stl.c src/view.c src/solid.c -lm

$(BUILD)/preview: tools/preview.c src/job.c src/stl.c src/slice.c src/view.c src/solid.c src/markup.h include/gs.h include/gs_job.h include/gs_view.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Isrc -I$(LAYOUT)/include -o $@ tools/preview.c src/job.c src/stl.c src/slice.c src/view.c src/solid.c \
	    $(LAYOUT)/ui/olui.c $(LAYOUT)/ui/olui_css.c $(LAYOUT)/ui/olui_markup.c $(LAYOUT)/ui/olui_theme_none.c \
	    $(LAYOUT)/build/libopenlayout.a -lm

$(BUILD)/test_edit: tests/test_edit.c src/edit.c src/model.c src/stl.c src/view.c src/solid.c include/gs_edit.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -o $@ tests/test_edit.c src/edit.c src/model.c src/stl.c src/view.c src/solid.c -lm

test: $(BUILD)/test_slice $(BUILD)/test_model $(BUILD)/test_gpu $(BUILD)/test_edit
	$(BUILD)/test_slice
	$(BUILD)/test_model
	$(BUILD)/test_gpu
	$(BUILD)/test_edit

preview: $(BUILD)/preview

clean:
	rm -rf $(BUILD)
