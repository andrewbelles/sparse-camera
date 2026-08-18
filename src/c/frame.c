/*
 * frame.c  Andrew Belles 
 *
 * Implementation of frame_t struct and its functionality.
 *
 * TODO:
 * - frame_t needs to be rewritten to act functionally like a ring buffer. 
 * - allocation needs to be a static arena for up to worst case for an event. 
 * - allocation of frame_t can only occur once at camera start-up; any 
 *   events should just flush the arena pointer 
 */
#include <stdlib.h> 
#include <time.h> 
#include <stdint.h> 
#include "frame.h" 

#define NANOSECONDS 1000000000LL


frame_t* new_frames(size_t capacity)
{
  if ( capacity == 0 ) {
    return NULL; 
  }
  
  frame_t* frame = (frame_t *)calloc(1, sizeof(frame_t)); 
  if ( !frame ) {
    return NULL; 
  }

  frame->support = (uint32_t *)malloc(capacity * sizeof(uint32_t)); 
  frame->signal  = (float *)malloc(capacity * sizeof(float));

  if ( !frame->support || !frame->signal ) {
    del_frames(frame); 
    return NULL; 
  }

  frame->capacity = (uint32_t)capacity; 
  frame_reset(frame); 

  return frame; 
}


void del_frames(frame_t* frame) 
{
  if ( !frame ) {
    return; 
  }

  free(frame->support); 
  free(frame->signal); 
  free(frame); 
}


int frame_reserve(frame_t *frame, size_t capacity)
{
  if ( !frame ) {
    return -1;
  }

  if ( capacity <= frame->capacity ) {
    return 0; 
  }

  uint32_t* support = (uint32_t *)realloc(frame->support, capacity * sizeof(uint32_t)); 
  if ( !support ) return -1; 
  float* signal    = (float *)realloc(frame->signal, capacity * sizeof(float)); 
  if ( !signal ) return -1; 

  frame->support  = support; 
  frame->signal   = signal; 
  frame->capacity = (uint32_t)capacity; 
  return 0; 
}


void frame_reset(frame_t* frame) 
{
  if ( !frame ) {
    return; 
  }

  frame->count    = 0; 
  frame->t_ns     = 0; 
  frame->sequence = 0; 
}

/*
 * Timestamps a frame in nanoseconds relative to the system 
 * defined reference point from a stamped timespec  
 *
 */
void timestamp_frame(frame_t* frame)
{
  if ( !frame ) {
    return;
  }

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts); 
  frame->t_ns = (int64_t)ts.tv_sec * NANOSECONDS + (int64_t)ts.tv_nsec;
}

