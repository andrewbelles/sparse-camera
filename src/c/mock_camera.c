/*
 * mock_camera.c  Opus 5 
 *
 * Synthetic camera backend for smoke-testing the acquisition -> wire -> sink
 * path without any hardware.
 *
 * Emits a band of sites contiguous cells that walks across a rows x cols
 * grid, one step per frame, paced to a requested fps off CLOCK_MONOTONIC.
 * Every value lands in [0, 1] and every index in [0, rows*cols), so a clean
 * run must report zero bad_index / bad_value / seq_gaps.
 *
 * Satisfies the camera_interface_t outlined in camera.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#include "mock_camera.h"
#include "camera.h"
#include "frame.h"

#define NANOSECONDS 1000000000LL

typedef struct mock_ctx {
  mock_config_t cfg;
  int64_t period_ns;
  int64_t next_ns;   // absolute wake deadline for the next frame
  uint64_t sequence; // stand-in for a driver frame counter
  bool up;
} mock_ctx_t;


static int64_t monotonic_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * NANOSECONDS + (int64_t)ts.tv_nsec;
}


static void sleep_until_ns(int64_t deadline_ns)
{
  struct timespec ts = {
    .tv_sec  = (time_t)(deadline_ns / NANOSECONDS),
    .tv_nsec = (long)(deadline_ns % NANOSECONDS)
  };

  // deadline is absolute, so restarting after a signal does not drift
  while ( clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR ) {
    ;
  }
}


mock_ctx_t* mock_ctx_create(const mock_config_t* cfg)
{
  if ( !cfg || cfg->rows == 0 || cfg->cols == 0 || cfg->fps <= 0.0 ) {
    return NULL;
  }

  mock_ctx_t* ctx = calloc(1, sizeof(mock_ctx_t)); // force zero

  if ( !ctx ) {
    return NULL;
  }

  ctx->cfg       = *cfg;
  ctx->period_ns = (int64_t)((double)NANOSECONDS / cfg->fps);

  if ( ctx->period_ns <= 0 ) {
    free(ctx);
    return NULL;
  }

  return ctx;
}


void mock_ctx_destroy(mock_ctx_t* ctx)
{
  free(ctx);
}


size_t mock_max_sites(const mock_ctx_t* ctx)
{
  if ( !ctx ) {
    return 0;
  }

  return (size_t)ctx->cfg.rows * ctx->cfg.cols;
}


/* ----------------------------------------------
 * camera_interface_t implementation
 * ---------------------------------------------- */

static int mock_init(void* impl_ctx)
{
  mock_ctx_t* ctx = (mock_ctx_t *)impl_ctx;

  if ( !ctx || ctx->period_ns <= 0 ) {
    return -1;
  }

  ctx->sequence = 0;
  ctx->next_ns  = monotonic_ns() + ctx->period_ns;
  ctx->up       = true;
  return 0;
}


/*
 * Idempotent, and safe when init never ran.
 */
static void mock_deinit(void* impl_ctx)
{
  mock_ctx_t* ctx = (mock_ctx_t *)impl_ctx;

  if ( !ctx ) {
    return;
  }

  ctx->up      = false;
  ctx->next_ns = 0;
}


/*
 * Blocks until this frame's deadline, then fills out. Stamps t_ns at wake
 * rather than at return, standing in for a driver capture stamp.
 */
static int mock_acquire(void* impl_ctx, frame_t* out)
{
  mock_ctx_t* ctx = (mock_ctx_t *)impl_ctx;

  if ( !ctx || !ctx->up || !out ) {
    return -1;
  }

  sleep_until_ns(ctx->next_ns);

  const int64_t t_ns = monotonic_ns();

  /*
   * Absolute deadlines hold the cadence without drift. If the thread was
   * descheduled past a deadline, resync rather than advancing by period
   * alone; advancing alone would emit the backlog with no delay between
   * frames.
   */
  ctx->next_ns += ctx->period_ns;

  if ( ctx->next_ns <= t_ns ) {
    ctx->next_ns = t_ns + ctx->period_ns;
  }

  const uint32_t total = ctx->cfg.rows * ctx->cfg.cols;

  uint32_t n = ctx->cfg.sites;

  if ( n > total )         n = total;
  if ( n > out->capacity ) n = out->capacity;

  // band walks one cell per frame so support changes every tick
  const uint32_t base = (uint32_t)(ctx->sequence % total);

  for ( uint32_t i = 0; i < n; i++ ) {
    const uint32_t site = (base + i) % total;

    out->support[i] = site;
    out->signal[i]  = (float)(site % 256u) / 255.0f;  // stays in [0, 1]
  }

  out->count    = n;
  out->t_ns     = t_ns;
  out->sequence = ctx->sequence++;

  return 0;
}


const camera_interface_t mock_camera_interface = {
  .init    = mock_init,
  .deinit  = mock_deinit,
  .acquire = mock_acquire
};
