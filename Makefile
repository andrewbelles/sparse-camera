# Makefile  sparse-camera  (Credit Opus 5)
#
#   make          build build/bin/smoke
#   make smoke    run the smoke test through configs/ini/smoke.ini
#   make clean    drop all build artifacts
#
# Everything the run does is configured in the INI, not on the command line.
# To use a different one:
#   make smoke CONFIG=other.ini

CC       ?= gcc
CFLAGS   ?= -std=gnu11 -O3 -g -Wall -Wextra -pthread
CPPFLAGS ?=
LDLIBS   := -lzmq -lm -pthread

SRC := src/c
OUT := build
OBJ := $(OUT)/obj
BIN := $(OUT)/bin

CONFIG ?= configs/ini/smoke.ini

SMOKE_OBJS := $(OBJ)/smoke.o $(OBJ)/sink.o $(OBJ)/frame.o $(OBJ)/camera.o \
              $(OBJ)/ini.o $(OBJ)/mock_camera.o $(OBJ)/v4l2_webcam.o

.PHONY: all smoke clean

all: $(BIN)/smoke

$(BIN)/smoke: $(SMOKE_OBJS) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(OBJ) $(BIN):
	mkdir -p $@

-include $(wildcard $(OBJ)/*.d)

smoke: $(BIN)/smoke
	@$(BIN)/smoke $(CONFIG)

clean:
	rm -rf $(OUT)
