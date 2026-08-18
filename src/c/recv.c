/*
 * recv.c  Andrew Belles 
 * 
 * Standalone fan-in consumer of cameras. Binds one PULL; every camera PUSHes into it. 
 *
 * After compilation; run via: 
 *    ./recv [endpoint] [n_frames] [expect_fps]
 *
 * n_frames 0 means run until Ctrl-C (SIGINT). Exits non-zero if any check failed 
 */ 
#include <stdio.h> 
#include <stdlib.h> 
#include <signal.h> 

#include "sink.h"

/*
 * SIGINT stop boilerplate 
 */

static volatile sig_atomic_t g_stop = 0; 

static void on_sigint(int s) {
  (void)s; 
  g_stop = 1; 
}


int main(int argc, char** argv)
{
  const char* endpoint = argc > 1? argv[1] : "ipc:///tmp/frames.sock"; 
  const uint64_t want  = argc > 2? strtoull(argv[2], NULL, 10) : 0; 
  const double fps     = argc > 3? strtod(argv[3], NULL) : 0.0; 
  const uint32_t sites = argc > 4? (uint32_t)strtoul(argv[4], NULL, 10) : 4096; 

  signal(SIGINT, on_sigint); 

  sink_check_t check = {
    .expect_period_ms = fps > 0.0? 1000.0 / fps : 0.0, 
    .tol              = 0.5, 
    .max_sites        = sites, 
    .verbose          = true 
  };

  sink_t* sink = sink_new(endpoint, &check); 

  if ( !sink ) {
    return 2; 
  }

  printf("listening on %s\n", endpoint); 

  sink_stats_t stats = {0}; 

  while ( !g_stop ) {
    int rc = sink_recv(sink, 250); 

    if ( rc < 0 ) { 
      break; 
    }

    sink_get_stats(sink, &stats); 

    if ( want && stats.received >= want ) {
      break;
    }
  }

  int rc = sink_report(sink); 
  sink_del(sink);
  return rc; 
}
