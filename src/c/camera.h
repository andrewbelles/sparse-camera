/*
 * camera.h  Andrew Belles 
 *
 * Camera interface/config that must be implemented by opaque backends to 
 * interact with sparse acquisition system and downstream pipeline.
 */ 
#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h> 
#include <stdbool.h> 
#include "frame.h"

/*
 * Contract/vtable every capture backend must implement. 
 *
 *  init    bring device up; returns 0 on success.
 *  deinit  tear down device; must be safe after a failed init. 
 *  acquire block until one frame is available, fill "out".
 *            - return 0 on success. 
 *            - return 1 on timeout. 
 *            - return negative on hard error. 
 *
 *  acquire() owns/stamps timestamp; should use driver's own capture stamp,
 *  not wall time at return. 
 */ 
typedef struct camera_interface {
  int  (*init)(void* impl_ctx); 
  void (*deinit)(void* impl_ctx); 
  int  (*acquire)(void* impl_ctx, frame_t* out); 
} camera_interface_t; 


typedef struct camera_ctx camera_ctx_t; 


typedef struct camera_config {
  const char* endpoint; // socket consumer binds to 
  void* zmq_ctx;        // shared context; NULL explicits camera creates own  
  uint32_t camera_id;   // id so fan-in can tell sources apart      
  size_t frame_cap;     // max sparse entries per frame 
  int sndhwm;           // outbound queue depth; 0 implies repo default 8
} camera_config_t; 


/*
 * Creates camera publisher and calls its implementation of init.  
 */
camera_ctx_t* camera_new(const camera_config_t* cfg, const camera_interface_t* interface, void* impl_ctx); 
void          camera_del(camera_ctx_t* camera); 

/*
 * One acquire + publish. 
 *
 * Returns: 
 * - 0 for frame sent 
 * - 1 for timeout or dropped send (queue full) 
 * - negative on hard error 
 *
 * Not thread safe; one thread per camera 
 */
int camera_tick(camera_ctx_t* camera); 


uint64_t camera_frames_sent(const camera_ctx_t* camera); 
uint64_t camera_frames_dropped(const camera_ctx_t* camera); 

#endif // !CAMERA_H
