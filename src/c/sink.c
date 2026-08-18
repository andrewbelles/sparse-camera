/*
 * sink.c  Andrew Belles 
 *
 * Implementation of interface/functionality for sink class 
 */

#include <asm-generic/errno-base.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <math.h> 
#include <errno.h> 
#include <zmq.h> 

#include "sink.h"
#include "frame.h"
#include "wire.h"

typedef struct {
  bool seen; 
  uint32_t camera_id; 
  uint64_t frames; 
  int64_t last_t_ns;
  uint64_t last_seq; 
  double dt_min_ms, dt_max_ms, dt_sum_ms; 
  uint64_t dt_n; 
} cam_state_t; 

struct sink {
  void* zmq_ctx; 
  void* socket; 
  frame_t* decoded; 
  sink_check_t check; 
  sink_stats_t stats; 
  cam_state_t cams[SINK_MAX_CAMERAS]; 
}; 


static cam_state_t* cam_find(sink_t* sink, uint32_t id)
{
  for ( uint32_t i = 0; i < SINK_MAX_CAMERAS; i++ ) {
    if ( sink->cams[i].seen && sink->cams[i].camera_id == id ) {
      return &sink->cams[i]; 
    }
  }

  for ( uint32_t i = 0; i < SINK_MAX_CAMERAS; i++ ) {
    if ( !sink->cams[i].seen ) {
      sink->cams[i].seen = true;
      sink->cams[i].camera_id = id; 
      sink->cams[i].dt_min_ms = 1e18; 
      sink->cams[i].dt_max_ms = -1e18; 
      sink->stats.cameras_seen++; 
      return &sink->cams[i]; 
    }
  }

  return NULL; 
}


static inline sink_t* sink_release(sink_t* sink)
{
  sink_del(sink); 
  return NULL; 
}

sink_t* sink_new(const char* endpoint, const sink_check_t* check)
{
  if ( !endpoint ) {
    return NULL; 
  }

  sink_t* sink = calloc(1, sizeof(*sink)); 

  if ( !sink ) {
    return NULL; 
  }

  if ( check ) {
    sink->check = *check;
  }

  if ( sink->check.tol <= 0.0 ) {
    sink->check.tol = 0.5; 
  }

  if ( !(sink->decoded = new_frames(sink->check.max_sites? sink->check.max_sites : 4096)) ) {
    fprintf(stderr, "sink: frame alloc failed\n"); 
    return sink_release(sink); 
  }

  if ( !(sink->zmq_ctx = zmq_ctx_new()) ) {
    fprintf(stderr, "sink: zmq_ctx_new failed: %s\n", zmq_strerror(errno));
    return sink_release(sink); 
  }

  if ( !(sink->socket = zmq_socket(sink->zmq_ctx, ZMQ_PULL)) ) {
    fprintf(stderr, "sink: zmq_socket failed: %s\n", zmq_strerror(errno)); 
    return sink_release(sink); 
  }

  int linger = 0; 
  zmq_setsockopt(sink->socket, ZMQ_LINGER, &linger, sizeof(linger)); 

  if ( zmq_bind(sink->socket, endpoint) != 0 ) {
    fprintf(stderr, "sink: bind(%s) failed: %s", endpoint, zmq_strerror(errno)); 
    return sink_release(sink); 
  }

  return sink; 
}


void sink_del(sink_t* sink)
{
  if ( !sink ) {
    return; 
  }
  
  if ( sink->socket )  zmq_close(sink->socket); 
  if ( sink->zmq_ctx ) zmq_ctx_destroy(sink->zmq_ctx);

  del_frames(sink->decoded); 
  free(sink); 
}


static void sink_validate(sink_t* sink, const frame_t* f)
{
  if ( !sink || !f ) {
    return; 
  }

  cam_state_t* cam = cam_find(sink, f->camera_id); 

  if ( !cam ) {
    fprintf(stderr, "sink: too many distinct camera ids (>%d)\n", SINK_MAX_CAMERAS); 
    return; 
  }

  double dt_ms = -1.0; 

  if ( cam->frames > 0 ) {
    dt_ms = (double)(f->t_ns - cam->last_t_ns) / 1e6; 

    if ( f->t_ns <= cam->last_t_ns ) {
      sink->stats.backwards++; 
      fprintf(stderr, "sink: cam %u seq %lu: stamp did not advance (%.3f ms)\n",
          f->camera_id, f->sequence, dt_ms);  
    } else if ( sink->check.expect_period_ms > 0.0 ) {
      const double p   = sink->check.expect_period_ms; 
      const double tol = sink->check.tol; 

      if ( dt_ms < p * (1.0 - tol) || dt_ms > p * (1.0 + tol) ) {
        sink->stats.jitter++; 
        fprintf(stderr, "sink: cam %u seq %lu: delta %.2f ms outside %.2f +/- %0.f%%\n",
            f->camera_id, f->sequence, dt_ms, p, tol * 100.0);
      }
    }

    if ( f->sequence != cam->last_seq + 1 ) {
      uint64_t lost = f->sequence > cam->last_seq? f->sequence - cam->last_seq - 1 : 0; 
      sink->stats.seq_gaps++; 
      sink->stats.seq_lost += lost; 

      fprintf(stderr, "sink: cam %u seq %lu -> %lu (%lu lost)\n",
          f->camera_id, cam->last_seq, f->sequence, lost); 
    }

    if ( dt_ms > 0.0 ) {
      if ( dt_ms < cam->dt_min_ms ) cam->dt_min_ms = dt_ms; 
      if ( dt_ms > cam->dt_max_ms ) cam->dt_max_ms = dt_ms; 

      cam->dt_sum_ms += dt_ms; 
      cam->dt_n++; 
    }
  }

  bool idx_bad = false, val_bad = false; 

  for ( uint32_t i = 0; i < f->count; i++ ) {
    const uint32_t s = f->support[i]; 
    const float   v  = f->signal[i]; 

    if ( (sink->check.max_sites && s >= sink->check.max_sites) ) {
      idx_bad = true; 
    }

    if ( !isfinite(v) || v < 0.0 || v > 1.0 ) {
      val_bad = true; 
    }
  }

  if ( idx_bad ) { 
    sink->stats.bad_index++; 
    fprintf(stderr, "sink: cam %u seq %lu: support index out of range\n",
        f->camera_id, f->sequence);
  }

  if ( val_bad ) {
    sink->stats.bad_value++; 
    fprintf(stderr, "sink: cam %u seq %lu: signal outside [0, 1] or non-finite\n",
        f->camera_id, f->sequence);
  }

  if ( sink->check.verbose ) {
    double t = (double)f->t_ns / 1e9; 
    if ( cam->frames > 0 ) {
      printf("cam %u seq %6lu sites %5u t %13.6f s dt %8.2f ms\n",
          f->camera_id, f->sequence, f->count, t, dt_ms); 
    } else {
      printf("cam %u seq %6lu sites %5u t %13.6f s dt       -- ms\n",
          f->camera_id, f->sequence, f->count, t);  
    }
  }

  cam->frames++; 
  cam->last_t_ns = f->t_ns; 
  cam->last_seq  = f->sequence;
}


int sink_recv(sink_t *sink, int timeout_ms)
{
  if ( !sink ) {
    return -1; 
  }

  zmq_pollitem_t item = {
    .socket = sink->socket, 
    .events = ZMQ_POLLIN 
  }; 

  int rc = zmq_poll(&item, 1, timeout_ms); 

  if ( rc == 0 ) {
    return 1; 
  }

  if ( rc < 0 ) {
    if ( errno == EINTR || errno == ETERM ) {
      return 1; 
    }

    fprintf(stderr, "sink: zmq_poll failed: %s\n", zmq_strerror(errno)); 
    return -2; 
  }

  zmq_msg_t msg; 
  zmq_msg_init(&msg); 

  if ( zmq_msg_recv(&msg, sink->socket, 0) < 0 ) {
    zmq_msg_close(&msg); 
    if ( errno == EAGAIN || errno == EINTR ) {
      return 1; 
    }
    fprintf(stderr, "sink: recv failed: %s\n", zmq_strerror(errno)); 
    return -3; 
  }

  int status = frame_wire_unpack(zmq_msg_data(&msg), zmq_msg_size(&msg), sink->decoded); 

  if ( status == FRAME_WIRE_ERR_CAPACITY ) {
    if ( frame_reserve(sink->decoded, sink->decoded->count) == 0 ) {
      status = frame_wire_unpack(zmq_msg_data(&msg), zmq_msg_size(&msg), sink->decoded); 
    }
  }

  sink->stats.received++; 

  if ( status != FRAME_WIRE_OK ) {
    sink->stats.bad_parse++; 
    fprintf(stderr, "sink: dropping %zu-byte message: %s\n", 
        zmq_msg_size(&msg), frame_wire_strerror(status)); 
  } else {
    sink_validate(sink, sink->decoded); 
  }

  zmq_msg_close(&msg); 
  return 0; 
}


void sink_get_stats(const sink_t *sink, sink_stats_t *out)
{
  if ( sink && out ) {
    *out = sink->stats; 
  }
}


int sink_report(const sink_t* sink)
{
  if ( !sink ) {
    return 1; 
  }

  const sink_stats_t* s = &sink->stats; 

  printf("- Sink Report: -\n"); 
  printf("received    %lu\n", s->received);
  printf("cameras     %u\n", s->cameras_seen);

  for ( uint32_t i = 0; i < SINK_MAX_CAMERAS; i++ ) {
    const cam_state_t* c = &sink->cams[i]; 

    if ( !c->seen ) {
      continue; 
    }

    printf("  cam %-3u frames %-6lu last_seq %-6lu",
        c->camera_id, c->frames, c->last_seq); 

    if ( c->dt_n ) {
      printf(" dt min/mean/max %.2f/%.2f/%.2f ms",
          c->dt_min_ms, c->dt_sum_ms / (double)c->dt_n, c->dt_max_ms); 
    }
    printf("\n"); 
  }

  printf("bad_parse       %lu\n", s->bad_parse);
  printf("stamp backwards %lu\n", s->backwards);
  printf("cadence jitter  %lu\n", s->jitter);
  printf("sequence gaps   %lu (%lu frames lost)\n",
         s->seq_gaps, s->seq_lost);
  printf("bad support idx %lu\n", s->bad_index);
  printf("bad signal val  %lu\n", s->bad_value);
 
  const bool failed = s->received == 0 || s->bad_parse || s->backwards ||
                      s->seq_gaps || s->bad_index || s->bad_value;
 
  printf("result          %s\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
