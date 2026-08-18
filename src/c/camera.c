/*
 * camera.c  Andrew Belles 
 *
 * Implementation of Camera Interface and Core Functionality 
 */

#include <stdlib.h> 
#include <stdio.h> 
#include <string.h> 
#include <errno.h> 
#include <zmq.h> 

#include "camera.h"
#include "frame.h"
#include "wire.h"

/*
 * Local to source definition of the camera context  
 */ 
struct camera_ctx {
  void* zmq_ctx; 
  void* socket;       // ZMQ_PUSH to consumer's PULL  
  frame_t* frame; 
  uint8_t* scratch;   // reused pack buffer 
  size_t scratch_cap; 
  uint32_t camera_id; 
  uint64_t sent; 
  uint64_t dropped; 
  void* impl_ctx; 
  bool owns_ctx;      // true if zmq_ctx_new called by own ctor 
  bool backend_up;    // true once init was attempted 
  const camera_interface_t* interface; 
};


static inline camera_ctx_t* camera_release(camera_ctx_t* camera)
{
  if ( camera ) camera_del(camera); 
  return NULL; 
}

camera_ctx_t* camera_new(const camera_config_t* cfg, const camera_interface_t* interface, void* impl_ctx)
{
  if ( !cfg || !cfg->endpoint || !interface || !interface->acquire || cfg->frame_cap == 0 ) {
    return NULL; 
  }

  camera_ctx_t* camera = calloc(1, sizeof(camera_ctx_t)); 
  if ( !camera ) {
    return NULL; 
  }

  camera->camera_id = cfg->camera_id; 
  camera->impl_ctx  = impl_ctx; 
  camera->interface = interface; 
  camera->frame     = new_frames(cfg->frame_cap); 

  if ( !camera->frame ) {
    fprintf(stderr, "camera: frame alloc failed\n"); 
    return camera_release(camera); 
  }

  camera->frame->camera_id = cfg->camera_id; 
  camera->scratch_cap      = sizeof(frame_wire_header_t) +
                             (size_t)camera->frame->capacity * (sizeof(uint32_t) + sizeof(float)); 
  camera->scratch          = malloc(camera->scratch_cap); 

  if ( !camera->scratch ) {
    fprintf(stderr, "camera: scratch alloc failed\n"); 
    return camera_release(camera); 
  }

  if ( cfg->zmq_ctx ) {
    camera->zmq_ctx = cfg->zmq_ctx;
  } else if ( !(camera->zmq_ctx = zmq_ctx_new()) ) {
    fprintf(stderr, "camera: zmq_ctx_new failed: %s\n", zmq_strerror(errno)); 
    return camera_release(camera); 
  } else {
    camera->owns_ctx = true; 
  }

  if ( !(camera->socket = zmq_socket(camera->zmq_ctx, ZMQ_PUSH)) ) {
    fprintf(stderr, "camera: zmq_socket failed %s\n", zmq_strerror(errno));
    return camera_release(camera); 
  }

  // high water mark at requested/default; bounded queue  
  int hwm = cfg->sndhwm > 0? cfg->sndhwm : 8; // defaults to 8 TODO magic number 
  int linger = 0; 
  zmq_setsockopt(camera->socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
  zmq_setsockopt(camera->socket, ZMQ_LINGER, &linger, sizeof(linger)); 

  if ( zmq_connect(camera->socket, cfg->endpoint) != 0 ) {
    fprintf(stderr, "camera: connect(%s) failed: %s\n", 
        cfg->endpoint, zmq_strerror(errno));
    return camera_release(camera); 
  }

  if ( interface->init ) {
    camera->backend_up = true; 
    if ( interface->init(impl_ctx) != 0 ) {
      fprintf(stderr, "camera: backend init failed\n"); 
      return camera_release(camera); 
    }
  }

  return camera; 
}


void camera_del(camera_ctx_t* camera)
{
  if ( !camera ) {
    return; 
  }

  if ( camera->interface && camera->interface->deinit ) {
    camera->interface->deinit(camera->impl_ctx); 
  }

  if ( camera->socket ) {
    zmq_close(camera->socket); 
  }

  if ( camera->owns_ctx && camera->zmq_ctx ) {
    zmq_ctx_destroy(camera->zmq_ctx);
  }

  del_frames(camera->frame); 
  free(camera->scratch);
  free(camera); 
}


int camera_tick(camera_ctx_t* camera)
{
  if ( !camera || !camera->interface ) {
    return -1; 
  }

  frame_t* frame = camera->frame; 
  frame_reset(frame); 
  frame->camera_id = camera->camera_id; 
  
  int rc = camera->interface->acquire(camera->impl_ctx, frame); 

  if ( rc != 0 ) {
    return rc; 
  }

  // backend did not give timestamp; fallback to system reference 
  if ( frame->t_ns == 0 ) {
    timestamp_frame(frame); 
  }

  size_t n = frame_wire_pack(frame, camera->scratch, camera->scratch_cap); 

  if ( n == 0 ) {
    fprintf(stderr, "camera: pack failed (count=%u, cap=%zu)", frame->count, camera->scratch_cap);
    return -2; 
  }

  if ( zmq_send(camera->socket, camera->scratch, n, ZMQ_DONTWAIT) < 0 ) {
    if ( errno == EAGAIN ) {
      camera->dropped++; 
      return 1; 
    }

    fprintf(stderr, "camera: zmq_send failed: %s\n", zmq_strerror(errno));
    return -3;
  }

  camera->sent++; 
  return 0; 
}


uint64_t camera_frames_sent(const camera_ctx_t *camera)
{
  return camera? camera->sent : 0; 
}

uint64_t camera_frames_dropped(const camera_ctx_t *camera)
{
  return camera? camera->dropped : 0; 
}
