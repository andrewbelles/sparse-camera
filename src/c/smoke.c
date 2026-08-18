/*
 * smoke.c  Andrew Belles
 *
 * Config-driven smoke test for the acquisition pipeline. Reads an INI file,
 * instantiates every enabled camera on its own thread, fans them all into one
 * sink, and validates what arrives.
 *
 * After compilation; run via:
 *    ./smoke [config.ini]
 *
 * Defaults to configs/ini/smoke.ini. Exits non-zero if any check failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <zmq.h>

#include "ini.h"
#include "sink.h"
#include "camera.h"
#include "mock_camera.h"
#include "v4l2_webcam.h"

#define DEFAULT_CONFIG "configs/ini/smoke.ini"
#define CAM_PREFIX     "cam."
#define ERRLEN         256
#define NANOSECONDS    1000000000LL

/*
 * SIGINT stop boilerplate
 */

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int s) {
  (void)s;
  g_stop = 1;
}


static int64_t monotonic_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * NANOSECONDS + (int64_t)ts.tv_nsec;
}


/*
 * Parses a "ROWSxCOLS" geometry value. Returns 0 on success.
 */
static int parse_grid(const char* s, uint32_t* rows, uint32_t* cols)
{
  unsigned r = 0, c = 0;
  char tail = '\0';

  if ( !s || sscanf(s, "%ux%u%c", &r, &c, &tail) != 2 || r == 0 || c == 0 ) {
    return -1;
  }

  *rows = r;
  *cols = c;
  return 0;
}


/* ----------------------------------------------
 * backend registry
 * ---------------------------------------------- */

typedef struct backend {
  const char* name;
  const camera_interface_t* interface;
  void*  (*create)(const ini_t* ini, const char* section, char* err, size_t errlen);
  void   (*destroy)(void* impl);
  size_t (*max_sites)(const void* impl);
} backend_t;


/* keys every camera section carries, whatever the backend */
#define COMMON_CAM_KEYS "backend", "enabled", "id", "fps"


static void* mock_from_ini(const ini_t* ini, const char* section,
                           char* err, size_t errlen)
{
  static const char* const allowed[] = { COMMON_CAM_KEYS, "grid", "sites" };

  if ( ini_check_keys(ini, section, allowed,
                      sizeof(allowed) / sizeof(*allowed), err, errlen) != 0 ) {
    return NULL;
  }

  const char* grid = NULL;
  mock_config_t cfg = { .rows = 64, .cols = 64, .sites = 256, .fps = 30.0 };

  if ( ini_get_str(ini, section, "grid", "64x64", &grid, err, errlen) != 0 ||
       ini_get_u32(ini, section, "sites", 256, &cfg.sites, err, errlen) != 0 ||
       ini_get_f64(ini, section, "fps", 30.0, &cfg.fps, err, errlen) != 0 ) {
    return NULL;
  }

  if ( parse_grid(grid, &cfg.rows, &cfg.cols) != 0 ) {
    snprintf(err, errlen, "[%s] grid: '%s' is not ROWSxCOLS", section, grid);
    return NULL;
  }

  if ( cfg.fps <= 0.0 ) {
    snprintf(err, errlen, "[%s] fps: must be positive", section);
    return NULL;
  }

  if ( cfg.sites == 0 ) {
    snprintf(err, errlen, "[%s] sites: must be positive", section);
    return NULL;
  }

  mock_ctx_t* ctx = mock_ctx_create(&cfg);

  if ( !ctx ) {
    snprintf(err, errlen, "[%s]: mock_ctx_create failed", section);
    return NULL;
  }

  return ctx;
}


static void   mock_destroy_v(void* impl) { mock_ctx_destroy((mock_ctx_t *)impl); }
static size_t mock_sites_v(const void* impl) { return mock_max_sites((const mock_ctx_t *)impl); }


static void* v4l2_from_ini(const ini_t* ini, const char* section,
                           char* err, size_t errlen)
{
  static const char* const allowed[] = {
    COMMON_CAM_KEYS, "device", "grid", "resolution", "threshold", "buffers", "timeout_ms"
  };

  if ( ini_check_keys(ini, section, allowed,
                      sizeof(allowed) / sizeof(*allowed), err, errlen) != 0 ) {
    return NULL;
  }

  const char* grid = NULL;
  const char* res  = NULL;
  uint32_t threshold = 32;
  v4l2_config_t cfg = { 0 };

  if ( ini_get_str(ini, section, "device", "/dev/video0", &cfg.device, err, errlen) != 0 ||
       ini_get_str(ini, section, "resolution", "480x640", &res, err, errlen) != 0 ||
       ini_get_str(ini, section, "grid", "64x64", &grid, err, errlen) != 0 ||
       ini_get_u32(ini, section, "threshold", 32, &threshold, err, errlen) != 0 ||
       ini_get_u32(ini, section, "buffers", 4, &cfg.n_buffers, err, errlen) != 0 ||
       ini_get_u32(ini, section, "timeout_ms", 2000, &cfg.timeout_ms, err, errlen) != 0 ) {
    return NULL;
  }

  if ( parse_grid(res, &cfg.height, &cfg.width) != 0 ) {
    snprintf(err, errlen, "[%s] resolution: '%s' is not HEIGHTxWIDTH", section, res);
    return NULL;
  }

  if ( parse_grid(grid, &cfg.grid_rows, &cfg.grid_cols) != 0 ) {
    snprintf(err, errlen, "[%s] grid: '%s' is not ROWSxCOLS", section, grid);
    return NULL;
  }

  if ( threshold > 255 ) {
    snprintf(err, errlen, "[%s] threshold: %u exceeds 255", section, threshold);
    return NULL;
  }
  cfg.threshold = (uint8_t)threshold;

  if ( cfg.n_buffers < 2 ) {
    snprintf(err, errlen, "[%s] buffers: need at least 2", section);
    return NULL;
  }

  v4l2_ctx_t* ctx = v4l2_ctx_create(&cfg);

  if ( !ctx ) {
    snprintf(err, errlen, "[%s]: v4l2_ctx_create failed", section);
    return NULL;
  }

  return ctx;
}


static void   v4l2_destroy_v(void* impl) { v4l2_ctx_destroy((v4l2_ctx_t *)impl); }
static size_t v4l2_sites_v(const void* impl) { return v4l2_max_sites((const v4l2_ctx_t *)impl); }


static const backend_t g_backends[] = {
  { "mock", &mock_camera_interface, mock_from_ini, mock_destroy_v, mock_sites_v },
  { "v4l2", &v4l2_camera_interface, v4l2_from_ini, v4l2_destroy_v, v4l2_sites_v },
};

#define N_BACKENDS (sizeof(g_backends) / sizeof(*g_backends))


static const backend_t* backend_find(const char* name)
{
  for ( size_t i = 0; i < N_BACKENDS; i++ ) {
    if ( strcmp(g_backends[i].name, name) == 0 ) {
      return &g_backends[i];
    }
  }

  return NULL;
}


/* ----------------------------------------------
 * camera threads
 * ---------------------------------------------- */

typedef struct cam_thread {
  char label[64];
  const backend_t* backend;
  void* impl;
  camera_ctx_t* camera;
  uint64_t want;     // frames to send; 0 runs until stop
  uint64_t ticks;
  int rc;
  bool started;
  pthread_t tid;
} cam_thread_t;


static void* cam_run(void* arg)
{
  cam_thread_t* ct = (cam_thread_t *)arg;

  while ( !g_stop && (ct->want == 0 || ct->ticks < ct->want) ) {
    int rc = camera_tick(ct->camera);

    if ( rc < 0 ) {
      fprintf(stderr, "smoke: %s: camera_tick failed (%d)\n", ct->label, rc);
      ct->rc = rc;
      break;
    }

    ct->ticks++;  // timeouts and drops still count as attempts
  }

  return NULL;
}


/*
 * Tears down whatever was built, in reverse. Safe on a partially-built table.
 */
static void cams_release(cam_thread_t* cams, size_t n)
{
  for ( size_t i = 0; i < n; i++ ) {
    if ( cams[i].camera ) {
      camera_del(cams[i].camera);   // calls the backend's deinit
      cams[i].camera = NULL;
    }

    if ( cams[i].impl ) {
      cams[i].backend->destroy(cams[i].impl);
      cams[i].impl = NULL;
    }
  }
}


/* ----------------------------------------------
 * config
 * ---------------------------------------------- */

typedef struct sink_conf {
  const char* endpoint;
  uint32_t frames;    // per camera; 0 runs until SIGINT
  double tol;
  uint32_t timeout_s;
  bool verbose;
} sink_conf_t;


static int load_sink_conf(const ini_t* ini, sink_conf_t* out, char* err, size_t errlen)
{
  static const char* const allowed[] = {
    "endpoint", "frames", "tol", "timeout_s", "verbose"
  };

  if ( !ini_has_section(ini, "sink") ) {
    snprintf(err, errlen, "config has no [sink] section");
    return -1;
  }

  if ( ini_check_keys(ini, "sink", allowed,
                      sizeof(allowed) / sizeof(*allowed), err, errlen) != 0 ) {
    return -1;
  }

  if ( ini_get_str(ini, "sink", "endpoint", "ipc:///tmp/frames.sock", &out->endpoint, err, errlen) != 0 ||
       ini_get_u32(ini, "sink", "frames", 100, &out->frames, err, errlen) != 0 ||
       ini_get_f64(ini, "sink", "tol", 0.5, &out->tol, err, errlen) != 0 ||
       ini_get_u32(ini, "sink", "timeout_s", 60, &out->timeout_s, err, errlen) != 0 ||
       ini_get_bool(ini, "sink", "verbose", true, &out->verbose, err, errlen) != 0 ) {
    return -1;
  }

  if ( out->tol <= 0.0 ) {
    snprintf(err, errlen, "[sink] tol: must be positive");
    return -1;
  }

  return 0;
}


/*
 * Walks every [cam.*] section, builds the enabled ones, and fills the spec
 * table the sink validates against. Returns the count, or -1 on error.
 */
static int build_cameras(const ini_t* ini, const sink_conf_t* sc, void* zmq_ctx,
                         cam_thread_t* cams, sink_cam_spec_t* specs,
                         char* err, size_t errlen)
{
  size_t n = 0;

  for ( size_t i = 0; i < ini_section_count(ini); i++ ) {
    const char* section = ini_section_name(ini, i);

    if ( strncmp(section, CAM_PREFIX, strlen(CAM_PREFIX)) != 0 ) {
      continue;
    }

    bool enabled = true;

    if ( ini_get_bool(ini, section, "enabled", true, &enabled, err, errlen) != 0 ) {
      return -1;
    }

    if ( !enabled ) {
      continue;
    }

    if ( n >= SINK_MAX_CAMERAS ) {
      snprintf(err, errlen, "more than %d enabled cameras", SINK_MAX_CAMERAS);
      return -1;
    }

    const char* name = NULL;

    if ( ini_get_str(ini, section, "backend", NULL, &name, err, errlen) != 0 ) {
      return -1;
    }

    if ( !name ) {
      snprintf(err, errlen, "[%s]: no backend given", section);
      return -1;
    }

    const backend_t* backend = backend_find(name);

    if ( !backend ) {
      snprintf(err, errlen, "[%s] backend: '%s' is not a known backend", section, name);
      return -1;
    }

    uint32_t id  = 0;
    double   fps = 0.0;

    if ( ini_get_u32(ini, section, "id", 0, &id, err, errlen) != 0 ||
         ini_get_f64(ini, section, "fps", 30.0, &fps, err, errlen) != 0 ) {
      return -1;
    }

    for ( size_t j = 0; j < n; j++ ) {
      if ( specs[j].camera_id == id ) {
        snprintf(err, errlen, "[%s] id: %u is already used by another camera", section, id);
        return -1;
      }
    }

    cam_thread_t* ct = &cams[n];
    memset(ct, 0, sizeof(*ct));
    snprintf(ct->label, sizeof(ct->label), "%s", section);
    ct->backend = backend;
    ct->want    = sc->frames;

    if ( !(ct->impl = backend->create(ini, section, err, errlen)) ) {
      return -1;  // create() already wrote a specific message
    }

    const size_t max_sites = backend->max_sites(ct->impl);

    if ( max_sites == 0 || max_sites > UINT32_MAX ) {
      snprintf(err, errlen, "[%s]: backend reports %zu sites", section, max_sites);
      return -1;
    }

    camera_config_t cfg = {
      .endpoint  = sc->endpoint,
      .zmq_ctx   = zmq_ctx,     // shared: one IO thread for every camera
      .camera_id = id,
      .frame_cap = max_sites,   // worst case for this backend's geometry
      .sndhwm    = 0
    };

    if ( !(ct->camera = camera_new(&cfg, backend->interface, ct->impl)) ) {
      snprintf(err, errlen, "[%s]: camera_new(%s) failed", section, sc->endpoint);
      return -1;
    }

    specs[n] = (sink_cam_spec_t){
      .camera_id        = id,
      .max_sites        = (uint32_t)max_sites,
      .expect_period_ms = fps > 0.0 ? 1000.0 / fps : 0.0
    };

    printf("smoke: %s -> backend %s, id %u, %zu sites max, %.1f fps\n",
           section, backend->name, id, max_sites, fps);
    n++;
  }

  return (int)n;
}


int main(int argc, char** argv)
{
  const char* path = argc > 1 ? argv[1] : DEFAULT_CONFIG;
  char err[ERRLEN] = {0};

  signal(SIGINT, on_sigint);

  ini_t* ini = ini_load(path, err, sizeof(err));

  if ( !ini ) {
    fprintf(stderr, "smoke: %s\n", err);
    return 2;
  }

  sink_conf_t sc = {0};

  if ( load_sink_conf(ini, &sc, err, sizeof(err)) != 0 ) {
    fprintf(stderr, "smoke: %s: %s\n", path, err);
    ini_free(ini);
    return 2;
  }

  /*
   * Sink binds before any camera connects, so no readiness handshake is
   * needed. Cameras share one context; the sink keeps its own, which is fine
   * over ipc and tcp.
   */
  sink_check_t check = {
    .expect_period_ms = 0.0,   // per-camera specs carry the real cadence
    .tol              = sc.tol,
    .max_sites        = 0,
    .verbose          = sc.verbose
  };

  sink_t* sink = sink_new(sc.endpoint, &check);

  if ( !sink ) {
    ini_free(ini);
    return 2;
  }

  void* zmq_ctx = zmq_ctx_new();

  if ( !zmq_ctx ) {
    fprintf(stderr, "smoke: zmq_ctx_new failed\n");
    sink_del(sink);
    ini_free(ini);
    return 2;
  }

  cam_thread_t cams[SINK_MAX_CAMERAS];
  sink_cam_spec_t specs[SINK_MAX_CAMERAS];
  memset(cams, 0, sizeof(cams));

  int n = build_cameras(ini, &sc, zmq_ctx, cams, specs, err, sizeof(err));

  if ( n < 0 ) {
    fprintf(stderr, "smoke: %s: %s\n", path, err);
    cams_release(cams, SINK_MAX_CAMERAS);
    zmq_ctx_destroy(zmq_ctx);
    sink_del(sink);
    ini_free(ini);
    return 2;
  }

  if ( n == 0 ) {
    fprintf(stderr, "smoke: %s: no cameras enabled\n", path);
    zmq_ctx_destroy(zmq_ctx);
    sink_del(sink);
    ini_free(ini);
    return 2;
  }

  if ( sink_expect(sink, specs, (size_t)n) != 0 ) {
    fprintf(stderr, "smoke: sink_expect failed\n");
    cams_release(cams, (size_t)n);
    zmq_ctx_destroy(zmq_ctx);
    sink_del(sink);
    ini_free(ini);
    return 2;
  }

  printf("smoke: listening on %s, %u frames per camera\n", sc.endpoint, sc.frames);

  for ( int i = 0; i < n; i++ ) {
    if ( pthread_create(&cams[i].tid, NULL, cam_run, &cams[i]) != 0 ) {
      fprintf(stderr, "smoke: %s: pthread_create failed\n", cams[i].label);
      g_stop = 1;
      break;
    }
    cams[i].started = true;
  }

  /*
   * Drain until every expected frame has landed. A producer that dropped on a
   * full queue means the total is never reached, so finishing also ends the
   * loop once the socket has gone quiet.
   */
  const uint64_t want_total = (uint64_t)sc.frames * (uint64_t)n;
  const int64_t deadline_ns = sc.timeout_s
                                ? monotonic_ns() + (int64_t)sc.timeout_s * NANOSECONDS
                                : 0;
  sink_stats_t stats = {0};
  int quiet = 0;
  bool timed_out = false;

  while ( !g_stop ) {
    int rc = sink_recv(sink, 250);

    if ( rc < 0 ) {
      break;
    }

    quiet = rc == 1 ? quiet + 1 : 0;

    sink_get_stats(sink, &stats);

    if ( want_total && stats.received >= want_total ) {
      break;
    }

    bool producing = false;

    for ( int i = 0; i < n && !producing; i++ ) {
      producing = cams[i].started &&
                  cams[i].rc == 0 &&
                  (cams[i].want == 0 || cams[i].ticks < cams[i].want);
    }

    if ( !producing && quiet >= 2 ) {
      break;  // everyone finished and nothing is still arriving
    }

    if ( deadline_ns && monotonic_ns() >= deadline_ns ) {
      fprintf(stderr, "smoke: timed out after %u s\n", sc.timeout_s);
      timed_out = true;
      break;
    }
  }

  g_stop = 1;  // release any producer still ticking

  for ( int i = 0; i < n; i++ ) {
    if ( cams[i].started ) {
      pthread_join(cams[i].tid, NULL);
    }
  }

  for ( int i = 0; i < n; i++ ) {
    printf("smoke: %s sent %lu, dropped %lu\n", cams[i].label,
           camera_frames_sent(cams[i].camera), camera_frames_dropped(cams[i].camera));
  }

  int rc = sink_report(sink);

  cams_release(cams, (size_t)n);
  zmq_ctx_destroy(zmq_ctx);
  sink_del(sink);
  ini_free(ini);

  return timed_out ? 1 : rc;
}
