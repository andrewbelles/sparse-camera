# Makefile  sparse-camera  (Credit Opus 5)
#
#   make          build recv + mock into build/bin
#   make smoke    run the mock producer against the recv consumer
#   make clean    drop all build artifacts
#
# Override on the command line, e.g.
#   make smoke FRAMES=500 FPS=60

CC       ?= gcc
CFLAGS   ?= -std=gnu11 -O3 -g -Wall -Wextra
CPPFLAGS ?=
LDLIBS   := -lzmq -lm

SRC := src/c
OUT := build
OBJ := $(OUT)/obj
BIN := $(OUT)/bin

ENDPOINT ?= ipc:///tmp/frames.sock
FRAMES   ?= 100
FPS      ?= 30

RECV_OBJS := $(OBJ)/recv.o $(OBJ)/sink.o $(OBJ)/frame.o
MOCK_OBJS := $(OBJ)/mock_camera.o $(OBJ)/camera.o $(OBJ)/frame.o

# v4l2_webcam.c is deliberately not built yet: it carries its own main() and
# defines v4l2_info_from_ctx as plain `inline`, which emits no symbol to link
# against. Add it here once both are resolved.

.PHONY: all smoke clean

all: $(BIN)/recv $(BIN)/mock

$(BIN)/recv: $(RECV_OBJS) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/mock: $(MOCK_OBJS) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(OBJ) $(BIN):
	mkdir -p $@

-include $(wildcard $(OBJ)/*.d)

# Consumer binds first, then one producer runs to completion. The consumer's
# exit status is the smoke test result: non-zero means a check failed.
smoke: all
	@rm -f /tmp/frames.sock
	@$(BIN)/recv $(ENDPOINT) $(FRAMES) $(FPS) & \
	 recv_pid=$$!; \
	 sleep 1; \
	 $(BIN)/mock $(ENDPOINT) $(FRAMES) $(FPS) 0; \
	 wait $$recv_pid

clean:
	rm -rf $(OUT)
