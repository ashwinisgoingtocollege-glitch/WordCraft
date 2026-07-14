SHELL := cmd.exe
.SHELLFLAGS := /C

APP ?= wordcraft.exe
BUILD_DIR ?= build
TOOLCHAIN ?= llvm

CPPFLAGS := -Iinclude -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -D_RICHEDIT_VER=0x0500
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS := -lcomctl32 -lcomdlg32 -lgdi32 -lole32 -loleaut32 -luuid -lshell32 -luser32 -lkernel32

ifeq ($(TOOLCHAIN),mingw)
CC := gcc
RC := windres
LDFLAGS := -municode -mwindows
RESOURCE := $(BUILD_DIR)/app_res.o
RC_COMMAND = $(RC) $(CPPFLAGS) -Iresources -O coff resources/app.rc -o $@
else
CC := clang
RC := llvm-rc
LDFLAGS := -fuse-ld=lld -municode -mwindows
RESOURCE := $(BUILD_DIR)/app.res
RC_COMMAND = $(RC) /nologo /I include /I resources /fo $@ resources/app.rc
endif

SOURCES := src/main.c src/document.c src/format.c src/dialogs.c src/printing.c src/text.c src/pageview.c src/language.c src/assist.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
RESOURCE_INPUTS := resources/app.rc resources/app.manifest resources/wordcraft.ico include/resource.h

.PHONY: all clean debug test gui-test

DEBUG_CFLAGS := -std=c11 -O0 -g -Wall -Wextra -Wpedantic

all: $(APP)

$(APP): $(OBJECTS) $(RESOURCE)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(RESOURCE) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c include/editor.h include/language.h include/resource.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RESOURCE): $(RESOURCE_INPUTS) | $(BUILD_DIR)
	$(RC_COMMAND)

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

debug:
	$(MAKE) APP=wordcraft-debug.exe BUILD_DIR=build-debug CFLAGS="$(DEBUG_CFLAGS)" all

$(BUILD_DIR)/wrap_probe.exe: tests/wrap_probe.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -luser32 -lkernel32

$(BUILD_DIR)/text_probe.exe: tests/text_probe.c src/text.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/text_probe.c src/text.c -lole32 -luser32 -lkernel32

$(BUILD_DIR)/rtf_probe.exe: tests/rtf_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -luser32 -lkernel32

$(BUILD_DIR)/language_probe.exe: tests/language_probe.c src/language.c include/language.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ tests/language_probe.c src/language.c -luser32 -lkernel32

test: all $(BUILD_DIR)/wrap_probe.exe $(BUILD_DIR)/text_probe.exe $(BUILD_DIR)/rtf_probe.exe $(BUILD_DIR)/language_probe.exe
	$(BUILD_DIR)\wrap_probe.exe
	$(BUILD_DIR)\text_probe.exe
	$(BUILD_DIR)\rtf_probe.exe
	$(BUILD_DIR)\language_probe.exe

$(BUILD_DIR)/gui_probe.exe: tests/gui_probe.c include/editor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -municode -o $@ $< -luser32 -lkernel32

gui-test: all $(BUILD_DIR)/gui_probe.exe
	$(BUILD_DIR)\gui_probe.exe

clean:
	if exist build rmdir /S /Q build
	if exist build-debug rmdir /S /Q build-debug
	if exist $(APP) del /Q $(APP)
	if exist wordcraft-debug.exe del /Q wordcraft-debug.exe
