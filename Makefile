# Makefile  sparse-camera  Opus 5/Andrew Belles
#
#   make setup      install dependencies and prepare the machine
#   make            build build/bin/smoke
#   make smoke      run the smoke test through configs/ini/smoke.ini
#   make memtest    run it under valgrind
#   make perf       profile it, breakdown lands in artifacts/
#   make clean      drop build/ and artifacts/
#
# A run is configured by its INI, not by the command line. To use a different
# one, pass it to the script directly:
#   scripts/memtest.sh configs/ini/other.ini

CC       ?= gcc
CFLAGS   ?= -std=gnu11 -O3 -g -Wall -Wextra -pthread
CPPFLAGS ?=
LDLIBS   := -lzmq -lm -pthread

SRC := src/c
OUT := build
OBJ := $(OUT)/obj
BIN := $(OUT)/bin
ART := artifacts

CONFIG ?= configs/ini/smoke.ini

SOURCES := $(SRC)/smoke.c $(SRC)/sink.c $(SRC)/frame.c $(SRC)/camera.c \
           $(SRC)/ini.c $(SRC)/mock_camera.c $(SRC)/v4l2_webcam.c

SMOKE_OBJS := $(OBJ)/smoke.o $(OBJ)/sink.o $(OBJ)/frame.o $(OBJ)/camera.o \
              $(OBJ)/ini.o $(OBJ)/mock_camera.o $(OBJ)/v4l2_webcam.o

# Shipped optimisation plus frame pointers, so sampled stacks can be walked.
PERF_CFLAGS := -std=gnu11 -O3 -g -fno-omit-frame-pointer -pthread

.PHONY: all setup smoke memtest perf clean

all: $(BIN)/smoke

setup:
	@scripts/setup.sh

$(BIN)/smoke: $(SMOKE_OBJS) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/smoke-perf: $(SOURCES) | $(BIN)
	$(CC) $(PERF_CFLAGS) $(CPPFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(OBJ) $(BIN):
	mkdir -p $@

-include $(wildcard $(OBJ)/*.d)

smoke: $(BIN)/smoke
	@$(BIN)/smoke $(CONFIG)

memtest: $(BIN)/smoke
	@scripts/memtest.sh

perf: $(BIN)/smoke-perf
	@scripts/perf.sh

clean:
	rm -rf $(OUT) $(ART)
