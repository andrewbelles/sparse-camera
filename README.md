# Overview

Sparse acquisition backend for an event driven camera written in C.

A camera backend implements the `camera_interface_t` vtable in `src/c/camera.h`
and produces sparse frames: a list of flat indices and their values. Frames are
packed into the wire format in `src/c/wire.h` and pushed over ZeroMQ to a sink
that validates timestamps, sequence numbers, indices, and values.

Two backends are included: `mock_camera.c`, which generates frames without
hardware, and `v4l2_webcam.c`, which captures YUYV from a V4L2 device.

## Future Work 

Future work will be dedicated to moving towards an event-driven backend. 
Cameras operating on their indepdent thread should only report a frame under 
a given event if a non-zero number of sites change. 

This means current available backends will need slight adjustments.

I'm also intending to purchase and wire in a CMOS camera for more precise 
measurements.

## Layout

```
src/c/
  camera.{c,h}       camera interface, publisher, per-frame tick
  frame.{c,h}        sparse frame storage
  wire.h             pack/unpack for the ZeroMQ message format
  sink.{c,h}         fan-in consumer and its validity checks
  mock_camera.{c,h}  synthetic backend
  v4l2_webcam.{c,h}  V4L2 backend
  ini.{c,h}          config file reader
  smoke.c            smoke test driver
configs/ini/         configuration files
```

## Requirements

- gcc with C11 support
- libzmq (`libzmq3-dev` on Debian and Ubuntu)
- Linux headers for the V4L2 backend

## Building

```
make
```

Produces `build/bin/smoke`. `make clean` removes `build/`.

## Running

```
make smoke
```

Runs the smoke test using `configs/ini/smoke.ini`. To use a different file:

```
make smoke CONFIG=configs/ini/other.ini
```

The binary can also be run directly, taking the config path as its only
argument:

```
build/bin/smoke configs/ini/smoke.ini
```

*_Note_*: It exits non-zero if any check fails. Currently this means the `v4l2` 
backend fails since it drops a frame on start-up. I'm debating whether it is worth
adding a guard against this or not. 

## Configuration

The config holds one `[sink]` section and one `[cam.<label>]` section per
camera. Each camera names a backend (`mock` or `v4l2`), a unique `id`, and
whatever keys that backend needs. Setting `enabled = false` skips a camera.
Unknown keys and malformed values are reported at startup.

The shipped config enables the mock only. To capture from a webcam, set
`enabled = true` in the `[cam.video0]` section and adjust `device`.

See `configs/ini/smoke.ini` for the available keys.
