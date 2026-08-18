/*
 * mock_camera.h  Andrew Belles
 *
 * Synthetic camera backend. Generates deterministic sparse frames with no
 * hardware, so the acquisition -> wire -> sink path can be exercised anywhere.
 */
#ifndef MOCK_CAMERA_H
#define MOCK_CAMERA_H

#include <stdint.h>
#include <stddef.h>
#include "camera.h"

typedef struct {
  uint32_t rows, cols; // output geometry; sites are flat indices into it
  uint32_t sites;      // sites emitted per frame, clamped to rows * cols
  double fps;          // pacing; acquire blocks to hold this cadence
} mock_config_t;

typedef struct mock_ctx mock_ctx_t;

mock_ctx_t* mock_ctx_create(const mock_config_t* cfg);
void        mock_ctx_destroy(mock_ctx_t* ctx);

/*
 * Number of sites a frame from this context can hold in the worst case.
 */
size_t mock_max_sites(const mock_ctx_t* ctx);

extern const camera_interface_t mock_camera_interface;

#endif // !MOCK_CAMERA_H
