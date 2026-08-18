/*
 * wire.h  Andrew Belles 
 */ 

#ifndef WIRE_H 
#define WIRE_H

#include <stdint.h> 
#include <string.h> 
#include "frame.h"

#define FRAME_WIRE_FORMAT 0x314d5246u // Frame wire format hexdump "FRM1" for format version 1
#define FRAME_WIRE_VERSION 1u 

/*
 * Header format preceding full packet over ZMQ, little-endian: 
 *
 *  [ 32-byte header ][ count * int32 support ][ count * float signal ]
 *
 * One ZMQ message holds an entire frame. Fixed header so consumer can read count before 
 * it knows how the long the message should be and so it can assert proper length. 
 */
typedef struct __attribute__((packed)) frame_wire_header {
  uint32_t format; 
  uint16_t version;
  uint16_t flags;  // reserved 
  uint32_t camera_id; 
  uint32_t count; 
  int64_t t_ns; 
  uint64_t sequence; 
} frame_wire_header_t; 

_Static_assert(sizeof(frame_wire_header_t) == 32, "wire header must be 32 bytes"); 

typedef enum {
  FRAME_WIRE_OK           = 0, 
  FRAME_WIRE_ERR_SHORT    = -1, 
  FRAME_WIRE_ERR_FORMAT   = -2, 
  FRAME_WIRE_ERR_VERSION  = -3, 
  FRAME_WIRE_ERR_LENGTH   = -4, 
  FRAME_WIRE_ERR_CAPACITY = -5
} frame_wire_status_t; 

/*
 * status ENUM into strerror for frame wire  
 */
static inline const char* frame_wire_strerror(int status)
{
  switch ( status ) {
    case FRAME_WIRE_OK: 
      return "ok"; 
    case FRAME_WIRE_ERR_SHORT: 
      return "message shorter than header"; 
    case FRAME_WIRE_ERR_FORMAT: 
      return "bad format"; 
    case FRAME_WIRE_ERR_VERSION: 
      return "bad version"; 
    case FRAME_WIRE_ERR_LENGTH: 
      return "length does not match count"; 
    case FRAME_WIRE_ERR_CAPACITY:
      return "destination frame too small"; 
    default: 
      return "unknown"; 
  }
}

static inline size_t frame_wire_size(const frame_t* frame)
{
  return sizeof(frame_wire_header_t) + (size_t)frame->count * (sizeof(uint32_t) + sizeof(float)); 
}

/*
 * Packs frame into out by creating the header, then stacking the support and signal together. 
 * Returns bytes written, or 0 if cap is too small.
 *
 * Assumes little-endian host (x86_64 / aarch64). 
 */
static inline size_t frame_wire_pack(const frame_t* frame, void* out, size_t cap)
{
  const size_t need = frame_wire_size(frame); 

  if ( !frame || !out || cap < need ) {
    return 0; 
  }

  frame_wire_header_t hdr = {
    .format    = FRAME_WIRE_FORMAT, 
    .version   = FRAME_WIRE_VERSION, 
    .flags     = 0, 
    .camera_id = frame->camera_id,
    .count     = frame->count, 
    .t_ns      = frame->t_ns, 
    .sequence  = frame->sequence 
  }; 

  /*
   * Using reference pointer starting at addr of out; 
   * pack each contiguous block of memory into out. 
   */ 

  uint8_t* p = (uint8_t *)out; 
  memcpy(p, &hdr, sizeof(hdr)); 
  p += sizeof(hdr); 

  memcpy(p, frame->support, (size_t)(frame->count * sizeof(uint32_t))); 
  p += (size_t)frame->count * sizeof(uint32_t); 

  memcpy(p, frame->signal, (size_t)(frame->count * sizeof(float))); 
  return need; 
}

/*
 * Validate that a received message is correctly. Copies out just the header. 
 */
static inline int frame_wire_peek(const void* msg, size_t len, frame_wire_header_t* hdr)
{
  if ( !msg || !hdr || len < sizeof(frame_wire_header_t) ) {
    return FRAME_WIRE_ERR_SHORT; 
  }

  memcpy(hdr, msg, sizeof(frame_wire_header_t)); 

  if ( hdr->format != FRAME_WIRE_FORMAT ) {
    return FRAME_WIRE_ERR_FORMAT; 
  }

  if ( hdr->version > FRAME_WIRE_VERSION ) {
    return FRAME_WIRE_ERR_VERSION; 
  }

  if ( len != sizeof(frame_wire_header_t) + (size_t)hdr->count * (sizeof(uint32_t) + sizeof(float)) ) {
    return FRAME_WIRE_ERR_LENGTH; 
  }

  return FRAME_WIRE_OK; 
}

/*
 * Validates a message and copies to out. Must already have capacity >= msg count  
 *
 * Arrays are memcpy'd rahter than aliased in place. Aliasing in place is UB
 * for the unaligned buffer and makes decoded frame outlive-able only as long as the message. 
 */
static inline int frame_wire_unpack(const void* msg, size_t len, frame_t* out)
{
  if ( !out ) {
    return FRAME_WIRE_ERR_SHORT; 
  }

  frame_wire_header_t hdr; 
  int status = frame_wire_peek(msg, len, &hdr); // copy out header and get ?error
  
  if ( status != FRAME_WIRE_OK ) {
    return status; // propagate peek's error forward 
  }

  if ( hdr.count > out->capacity ) {
    out->count = hdr.count;  // caller needs to know how much it needs 
    return FRAME_WIRE_ERR_CAPACITY; 
  }

  const uint8_t* p = (uint8_t *)msg + sizeof(frame_wire_header_t); 
  memcpy(out->support, p, (size_t)hdr.count * sizeof(uint32_t)); 

  p += (size_t)hdr.count * sizeof(uint32_t); 
  memcpy(out->signal, p, (size_t)hdr.count * sizeof(float)); 

  out->camera_id = hdr.camera_id; 
  out->count     = hdr.count; 
  out->t_ns      = hdr.t_ns; 
  out->sequence  = hdr.sequence; 

  return FRAME_WIRE_OK; 
}

#endif // !WIRE_H 
