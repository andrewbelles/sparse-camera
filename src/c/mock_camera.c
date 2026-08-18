/*
 * mock_camera.c  Andrew Belles
 *
 * Synthetic camera backend + standalone driver for smoke-testing the
 * acquisition -> wire -> sink path without any hardware.
 *
 * Emits a band of `sites` contiguous cells that walks across a rows x cols
 * grid, one step per frame, paced to a requested fps off CLOCK_MONOTONIC.
 * Every value lands in [0, 1] and every index in [0, rows*cols), so a clean
 * run must report zero bad_index / bad_value / seq_gaps.
 *
 * Run:
 *    ./mock [endpoint] [n_frames] [fps] [camera_id]
 *
 * n_frames 0 means run until Ctrl-C (SIGINT).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "camera.h"
#include "frame.h"

#define NANOSECONDS 1000000000LL

#define MOCK_ROWS  64u
#define MOCK_COLS  64u
#define MOCK_SITES 256u

typedef struct mock_ctx {
  uint32_t rows, cols;
  uint32_t sites;     // sites emitted per frame
  int64_t  period_ns;
  int64_t  next_ns;   // absolute wake deadline for the next frame
  uint64_t sequence;  // stand-in for a driver frame counter
  bool     up;
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


/* ----------------------------------------------
 * camera_interface_t implementation
 * ---------------------------------------------- */

static int mock_init(void* impl_ctx)
{
  mock_ctx_t* ctx = (mock_ctx_t *)impl_ctx;

  if ( !ctx || ctx->rows == 0 || ctx->cols == 0 || ctx->period_ns <= 0 ) {
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
  ctx->next_ns += ctx->period_ns;

  const int64_t t_ns   = monotonic_ns();
  const uint32_t total = ctx->rows * ctx->cols;

  uint32_t n = ctx->sites;

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


static const camera_interface_t mock_camera_interface = {
  .init    = mock_init,
  .deinit  = mock_deinit,
  .acquire = mock_acquire
};


/* ----------------------------------------------
 * driver
 * ---------------------------------------------- */

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int s) {
  (void)s;
  g_stop = 1;
}


int main(int argc, char** argv)
{
  const char* endpoint = argc > 1 ? argv[1] : "ipc:///tmp/frames.sock";
  const uint64_t want  = argc > 2 ? strtoull(argv[2], NULL, 10) : 100;
  const double fps     = argc > 3 ? strtod(argv[3], NULL) : 30.0;
  const uint32_t id    = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 0;

  if ( fps <= 0.0 ) {
    fprintf(stderr, "mock: fps must be positive\n");
    return 2;
  }

  signal(SIGINT, on_sigint);

  mock_ctx_t mock = {
    .rows      = MOCK_ROWS,
    .cols      = MOCK_COLS,
    .sites     = MOCK_SITES,
    .period_ns = (int64_t)(NANOSECONDS / fps)
  };

  camera_config_t cfg = {
    .endpoint  = endpoint,
    .zmq_ctx   = NULL,          // standalone process; own context
    .camera_id = id,
    .frame_cap = (size_t)MOCK_ROWS * MOCK_COLS,
    .sndhwm    = 0
  };

  camera_ctx_t* camera = camera_new(&cfg, &mock_camera_interface, &mock);

  if ( !camera ) {
    fprintf(stderr, "mock: camera_new(%s) failed\n", endpoint);
    return 2;
  }

  printf("mock cam %u -> %s, %ux%u grid, %u sites, %.1f fps\n",
         id, endpoint, MOCK_ROWS, MOCK_COLS, MOCK_SITES, fps);

  uint64_t ticks = 0;
  int rc = 0;

  while ( !g_stop && (want == 0 || ticks < want) ) {
    rc = camera_tick(camera);

    if ( rc < 0 ) {
      fprintf(stderr, "mock: camera_tick failed (%d)\n", rc);
      break;
    }

    ticks++;  // timeouts and drops still count as attempts
  }

  printf("mock cam %u: sent %lu, dropped %lu\n",
         id, camera_frames_sent(camera), camera_frames_dropped(camera));

  camera_del(camera);
  return rc < 0 ? 1 : 0;
}
