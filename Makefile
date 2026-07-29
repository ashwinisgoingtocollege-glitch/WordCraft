SHELL := cmd.exe
.SHELLFLAGS := /C

APP ?= wordcraft.exe
BUILD_DIR ?= build
TOOLCHAIN ?= llvm

CPPFLAGS := -Iinclude -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -D_RICHEDIT_VER=0x0500
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS := -lcomctl32 -lcomdlg32 -ld2d1 -lgdi32 -limm32 -lole32 -loleaut32 -luuid -lshell32 -lws2_32 -luser32 -lkernel32

ifeq ($(TOOLCHAIN),mingw)
CC := gcc
CXX := g++
RC := windres
LDFLAGS := -municode -mwindows
CONSOLE_LDFLAGS := -municode
RESOURCE := $(BUILD_DIR)/app_res.o
RC_COMMAND = $(RC) $(CPPFLAGS) -Iresources -O coff resources/app.rc -o $@
else
CC := clang
CXX := clang++
RC := llvm-rc
LDFLAGS := -fuse-ld=lld -municode -mwindows
CONSOLE_LDFLAGS := -fuse-ld=lld -municode
RESOURCE := $(BUILD_DIR)/app.res
RC_COMMAND = $(RC) /nologo /I include /I resources /fo $@ resources/app.rc
endif

SOURCES := src/main.c src/document.c src/format.c src/dialogs.c src/insert.c src/draw.c src/printing.c src/text.c src/pageview.c src/language.c src/assist.c src/fonts.c src/ribbon.c src/comments.c src/history.c src/textengine.c src/splash.c src/paper.c src/live.c src/liveui.c
CXX_SOURCES := src/rendereditor.cpp
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES)) $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CXX_SOURCES))
RESOURCE_INPUTS := resources/app.rc resources/app.manifest resources/wordcraft.ico include/resource.h

.PHONY: all clean debug test gui-test

DEBUG_CFLAGS := -std=c11 -O0 -g -Wall -Wextra -Wpedantic
DEBUG_CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -Wpedantic

all: $(APP)

$(APP): $(OBJECTS) $(RESOURCE)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(RESOURCE) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c include/editor.h include/fonts.h include/language.h include/live.h include/paper.h include/rendereditor.h include/resource.h include/splash.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rendereditor.o: src/rendereditor.cpp include/editor.h include/rendereditor.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(RESOURCE): $(RESOURCE_INPUTS) | $(BUILD_DIR)
	$(RC_COMMAND)

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

debug:
	$(MAKE) APP=wordcraft-debug.exe BUILD_DIR=build-debug CFLAGS="$(DEBUG_CFLAGS)" CXXFLAGS="$(DEBUG_CXXFLAGS)" all

$(BUILD_DIR)/wrap_probe.exe: tests/wrap_probe.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -luser32 -lkernel32

$(BUILD_DIR)/text_probe.exe: tests/text_probe.c src/text.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/text_probe.c src/text.c -lole32 -luser32 -lkernel32

$(BUILD_DIR)/rtf_probe.exe: tests/rtf_probe.c src/document.c include/editor.h include/rendereditor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/rtf_probe.c src/document.c -lcomdlg32 -luser32 -lkernel32

$(BUILD_DIR)/language_probe.exe: tests/language_probe.c src/language.c include/language.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/language_probe.c src/language.c -luser32 -lkernel32

$(BUILD_DIR)/font_probe.exe: tests/font_probe.c src/fonts.c include/editor.h include/fonts.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/font_probe.c src/fonts.c -lgdi32 -luser32 -lkernel32

$(BUILD_DIR)/comment_probe.exe: tests/comment_probe.c src/comments.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/comment_probe.c src/comments.c -lgdi32 -lole32 -luser32 -lkernel32

$(BUILD_DIR)/textengine_probe.exe: tests/textengine_probe.c src/textengine.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/textengine_probe.c src/textengine.c -luser32 -lkernel32

$(BUILD_DIR)/paper_probe.exe: tests/paper_probe.c src/paper.c include/editor.h include/paper.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/paper_probe.c src/paper.c -luser32 -lkernel32

$(BUILD_DIR)/renderer_probe.o: tests/renderer_probe.c include/editor.h include/rendereditor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/renderer_probe.exe: $(BUILD_DIR)/renderer_probe.o $(BUILD_DIR)/rendereditor.o
	$(CXX) $(CONSOLE_LDFLAGS) -o $@ $^ -ld2d1 -lgdi32 -limm32 -lole32 -loleaut32 -luuid -luser32 -lkernel32

$(BUILD_DIR)/live_probe.exe: tests/live_probe.c src/live.c include/live.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/live_probe.c src/live.c -lws2_32 -luser32 -lkernel32

test: all $(BUILD_DIR)/wrap_probe.exe $(BUILD_DIR)/text_probe.exe $(BUILD_DIR)/rtf_probe.exe $(BUILD_DIR)/language_probe.exe $(BUILD_DIR)/font_probe.exe $(BUILD_DIR)/comment_probe.exe $(BUILD_DIR)/textengine_probe.exe $(BUILD_DIR)/paper_probe.exe $(BUILD_DIR)/renderer_probe.exe $(BUILD_DIR)/live_probe.exe
	$(BUILD_DIR)\wrap_probe.exe
	$(BUILD_DIR)\text_probe.exe
	$(BUILD_DIR)\rtf_probe.exe
	$(BUILD_DIR)\language_probe.exe
	$(BUILD_DIR)\font_probe.exe
	$(BUILD_DIR)\comment_probe.exe
	$(BUILD_DIR)\textengine_probe.exe
	$(BUILD_DIR)\paper_probe.exe
	$(BUILD_DIR)\renderer_probe.exe
	set WORDCRAFT_DISABLE_D2D=1&& $(BUILD_DIR)\renderer_probe.exe
	$(BUILD_DIR)\live_probe.exe

$(BUILD_DIR)/gui_probe.exe: tests/gui_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -lgdi32 -luser32 -lkernel32

$(BUILD_DIR)/splash_probe.exe: tests/splash_probe.c include/editor.h include/splash.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -lgdi32 -luser32 -lkernel32

$(BUILD_DIR)/live_gui_probe.exe: tests/live_gui_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -luser32 -lkernel32

$(BUILD_DIR)/history_gui_probe.exe: tests/history_gui_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -luser32 -lkernel32

$(BUILD_DIR)/draw_canvas_gui_probe.exe: tests/draw_canvas_gui_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -lgdi32 -lole32 -luuid -luser32 -lkernel32

gui-test: all $(BUILD_DIR)/gui_probe.exe $(BUILD_DIR)/splash_probe.exe $(BUILD_DIR)/live_gui_probe.exe $(BUILD_DIR)/history_gui_probe.exe $(BUILD_DIR)/draw_canvas_gui_probe.exe
	$(BUILD_DIR)\gui_probe.exe
	$(BUILD_DIR)\splash_probe.exe
	set WORDCRAFT_DISABLE_D2D=1&& $(BUILD_DIR)\splash_probe.exe
	$(BUILD_DIR)\live_gui_probe.exe
	$(BUILD_DIR)\history_gui_probe.exe
	$(BUILD_DIR)\draw_canvas_gui_probe.exe

clean:
	if exist build rmdir /S /Q build
	if exist build-debug rmdir /S /Q build-debug
	if exist $(APP) del /Q $(APP)
	if exist wordcraft-debug.exe del /Q wordcraft-debug.exe
