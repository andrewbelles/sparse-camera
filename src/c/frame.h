/*
 * frame.h  Andrew Belles 
 *
 * frame_t transparent definition and interface.
 *
 * See frame.c for TODO 
 */ 
#ifndef FRAME_H
#define FRAME_H 

#include <stdlib.h> 
#include <stdio.h> 
#include <stdint.h> 

/*
 * A frame is a sparse snapshot/event of one camera image. 
 *
 * support[i] flattened index (row * width + col) of the ith non-zero site 
 * signal[i]  value at that site 
 * count      number of live entries in support/signal for this frame 
 * capacity   allocated length of support
 *
 */
typedef struct frame {
  uint32_t* support; 
  float* signal; 
  int64_t t_ns;      // CLOCK_MONOTONIC nanoseconds; from driver stamp 
  uint64_t sequence; // driver-side frame counter; gap implies dropped frames 
  uint32_t camera_id; 
  uint32_t count; 
  uint32_t capacity; 
} frame_t; 

/*
 * Allocates one frame with room "capacity" for entries 
 */
frame_t* new_frames(size_t capacity);
void     del_frames(frame_t* frame); 

/*
 * Grow support/signal to hold at least capacity entries. Mirrors C++ .reserve() method in  
 * expected behavior. 
 *
 * Returns 0 on sucess, -1 on allocation failure (frame left usable). 
 */
int frame_reserve(frame_t* frame, size_t capacity); 

/* 
 * Without dropping allocation: resets count, t_ns, and sequence 
 */
void frame_reset(frame_t* frame); 

/*
 * Fallback stamp if the driver's buffer timestamp is available. 
 * This measures when userspace gets around to calling it, not capture time. 
 */ 
void timestamp_frame(frame_t* frame);

#endif // !FRAME_H
