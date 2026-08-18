/*
 * v4l2_webcam.c  Andrew Belles 
 *
 * Working Camera Backend using V4L2 Linux Webcam for testing sparse-acquisition. 
 *
 * Satisfies the camera_interface_t outlined in camera.h 
 */

#include <stdio.h> 
#include <stdlib.h> 
#include <stdint.h> 
#include <string.h> 
#include <stdbool.h>

#include <errno.h> 
#include <fcntl.h> 
#include <poll.h> 
#include <sys/ioctl.h> 
#include <sys/mman.h> 
#include <unistd.h> 
#include <linux/videodev2.h> 

#include "v4l2_webcam.h"
#include "frame.h"
#include "camera.h"

#define NANOSECONDS 1000000000LL 

/*
 * Unsure if this is strictly necessary; mostly just scaffolding to get the backend working  
 */
typedef struct {
  void* start; 
  size_t length; 
} mmap_buf_t; 


typedef struct grid_info {
  size_t rows, cols; // max rows/cols 
  float thr;         // threshold to listen for  
} grid_info_t; 


typedef struct v4l2_ctx {
  v4l2_config_t cfg; 
  int fd; 
  mmap_buf_t* buffers; 
  uint32_t n_buffers; 
  uint32_t width, height; 
  uint32_t pixelformat; 
  uint64_t frame_id;
  uint8_t* debug_frame; 
  bool streaming; 
  bool stamp_checked; 
  bool debug_valid; 
} v4l2_ctx_t; 


int xioctl(int fd, uint64_t req, void* arg)
{
  int r; 

  do {
    r = ioctl(fd, req, arg); 
  } while ( r == -1 && errno == EINTR);

  return r; 
}


v4l2_ctx_t* v4l2_ctx_create(const v4l2_config_t* cfg)
{
  if ( !cfg ) {
    return NULL; 
  }

  v4l2_ctx_t* ctx = calloc(1, sizeof(*ctx)); // force zero
  
  if ( !ctx ) {
    return NULL; 
  }

  ctx->cfg = *cfg; 
  ctx->fd  = -1; 
  return ctx; 
}


void v4l2_ctx_get_format(
    const v4l2_ctx_t* ctx, uint32_t* width, uint32_t* height, 
    uint32_t* pixelformat, uint32_t* n_buffers)
{
  if ( !ctx ) {
    return; 
  }

  if ( width )       *width       = ctx->width; 
  if ( height )      *height      = ctx->height; 
  if ( pixelformat ) *pixelformat = ctx->pixelformat; 
  if ( n_buffers )   *n_buffers   = ctx->n_buffers; 
}


static inline grid_info_t v4l2_info_from_ctx(const v4l2_ctx_t* __restrict__ ctx)
{
  return (grid_info_t){
    .rows = ctx->cfg.grid_rows? ctx->cfg.grid_rows : ctx->height, 
    .cols = ctx->cfg.grid_cols? ctx->cfg.grid_cols : ctx->width,
    .thr  = (float)ctx->cfg.threshold 
  };
}


inline size_t v4l2_max_sites(const v4l2_ctx_t* __restrict__ ctx) 
{
  if ( !ctx ) {
    return 0; 
  }
  
  const grid_info_t gi = v4l2_info_from_ctx(ctx);
  return gi.rows * gi.cols; 
}


static int v4l2_check_capabilities(v4l2_ctx_t* ctx)
{
  if ( !ctx ) {
    return -1; 
  }
  
  struct v4l2_capability cap = {0};  

  if ( xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0 ) {
    fprintf(stderr, "v4l2: QUERYCAP failed: %s", strerror(errno));
    return 1; 
  }

  if ( !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
       !(cap.capabilities & V4L2_CAP_STREAMING) ) {
    fprintf(stderr, "v4l2: device lacks capture support\n"); 
    return 2; 
  }

  return 0; 
}


/*
 * Requests the config structures format from ioctl; 
 * verifies it matches expectation and required YUYV format, 
 * then maps the context. 
 */
static int v4l2_set_fmt(v4l2_ctx_t* ctx, const v4l2_config_t* cfg)
{
  if ( !ctx || !cfg ) {
    return -1; 
  }

  struct v4l2_format fmt = {0}; 
  
  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  fmt.fmt.pix.width       = cfg->width; 
  fmt.fmt.pix.height      = cfg->height; 
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; 
  fmt.fmt.pix.field       = V4L2_FIELD_NONE;

  if ( xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0 ) {
    fprintf(stderr, "v4l2: S_FMT failed: %s\n", strerror(errno)); 
    return 1; 
  } 

  if ( fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ) {
    fprintf(stderr, "v4l2: driver would not give YUYV\n"); 
    return 2; 
  }

  /* warn if requested size does match
   * what is being returned 
   */ 

  if ( fmt.fmt.pix.width != cfg->width ||
      fmt.fmt.pix.height != cfg->height ) {
    fprintf(stderr, "v4l2: driver adjusted %ux%u to %ux%u\b", 
        cfg->width, cfg->width, fmt.fmt.pix.width, fmt.fmt.pix.height); 
  }

  ctx->width = fmt.fmt.pix.width; 
  ctx->height = fmt.fmt.pix.height; 
  ctx->pixelformat = fmt.fmt.pix.pixelformat; 

  return 0; 
}


static int v4l2_req_buffers(v4l2_ctx_t* ctx, const v4l2_config_t* cfg)
{
  if ( !ctx || !cfg ) {
    return -1; 
  }

  struct v4l2_requestbuffers req = {0}; 
  req.count  = cfg->n_buffers; 
  req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  req.memory = V4L2_MEMORY_MMAP; 

  if ( xioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0 ) {
    fprintf(stderr, "v4l2: REQBUFS failed: %s\n", strerror(errno)); 
    return 1;  
  }

  if ( req.count < 2 ) {
    fprintf(stderr, "v4l2: driver only granted %u buffers\n", req.count); 
    return 2; 
  }

  if ( !(ctx->buffers = calloc(req.count, sizeof(mmap_buf_t))) ) {
    return 3; 
  } 
  ctx->n_buffers = req.count;  

  return 0; 
}


static int v4l2_map_buffers(v4l2_ctx_t* ctx)
{
  if ( !ctx ) {
    return -1; 
  }

  for ( size_t i= 0; i < ctx->n_buffers; i++ ) {
    struct v4l2_buffer buf = {0}; 
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    buf.memory = V4L2_MEMORY_MMAP; 
    buf.index  = i; 

    if ( xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0 ) {
      fprintf(stderr, "v4l2: QUERYBUF failed: %s\n", strerror(errno));
      return 1; 
    }

    ctx->buffers[i].length = buf.length; 
    ctx->buffers[i].start  = mmap(
        NULL, buf.length, 
        PROT_READ | PROT_WRITE,
        MAP_SHARED, 
        ctx->fd, 
        buf.m.offset 
    ); 

    if ( ctx->buffers[i].start == MAP_FAILED ) {
      fprintf(stderr, "v4l2: mmap failed: %s\n", strerror(errno)); 
      return 2; 
    }

    if ( xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0 ) {
      fprintf(stderr, "v4l2: initial QBUF failed: %s\n", strerror(errno)); 
      return 3; 
    }
  }

  return 0; 
}


static int v4l2_validate_streamon(v4l2_ctx_t* ctx)
{
  if ( !ctx ) {
    return -1; 
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 

  if ( xioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
    fprintf(stderr, "v4l2: STREAMON failed: %s\n", strerror(errno)); 
    return 1; 
  }

  return 0; 
}

/*
 * Releases device resources without freeing the context allocation.
 * This tears the session down without invalidating the callers pointer. 
 *
 * Safe to call during any point in a partially-completed v4l2 initialization 
 */
static void v4l2_release(v4l2_ctx_t* ctx)
{
  if ( !ctx ) {
    return; 
  }

  if ( ctx->streaming ) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    xioctl(ctx->fd, VIDIOC_STREAMOFF, &type); 
    ctx->streaming = false; 
  }

  if ( ctx->buffers ) {
    for ( uint32_t i = 0; i < ctx->n_buffers; i++ ) {
      if ( ctx->buffers[i].start ) {
        munmap(ctx->buffers[i].start, ctx->buffers[i].length); 
      }
    }
    
    free(ctx->buffers); 
    ctx->buffers = NULL; 

  }

  if ( ctx->fd >= 0 ) {
    close(ctx->fd); 
    ctx->fd = -1; 
  }

  free(ctx->debug_frame); 
  ctx->debug_frame = NULL; 
  ctx->debug_valid = false; 
  ctx->n_buffers   = 0; 
}


void v4l2_ctx_destroy(v4l2_ctx_t* ctx)
{
  if ( !ctx ) {
    return; 
  }

  v4l2_release(ctx); 
  free(ctx); 
}


/* ----------------------------------------------
 * camera_interface_t implementation 
 * ---------------------------------------------- */  

static int v4l2_init(void* impl_ctx)
{
  v4l2_ctx_t* ctx = (v4l2_ctx_t *)impl_ctx; 
  const v4l2_config_t* cfg = &ctx->cfg; 

  ctx->fd = open(cfg->device, O_RDWR);

  if ( ctx->fd < 0 ) {
    fprintf(stderr, "v4l2: open(%s) failed\n", cfg->device);
    return -1; 
  }

  if ( v4l2_check_capabilities(ctx) != 0 ) return -1; 
  if ( v4l2_set_fmt(ctx, cfg) != 0 )       return -1;   
  if ( v4l2_req_buffers(ctx, cfg) != 0 )   return -1; 
  if ( v4l2_map_buffers(ctx) != 0 )        return -1; 
  if ( v4l2_validate_streamon(ctx) != 0 )  return -1; 

  if ( !(ctx->debug_frame = malloc((size_t)ctx->width * (size_t)ctx->height * 2)) ) {
    fprintf(stderr, "v4l2: debug frame alloc failed\n"); 
    return -1; 
  } 

  return 0; 
}


static void v4l2_deinit(void* impl_ctx)
{
  if ( !impl_ctx ) {
    return; 
  }

  v4l2_ctx_t* ctx = (v4l2_ctx_t *)impl_ctx;
  v4l2_release(ctx); 
}


/*
 * Map YUYV data to sparse sites that changed in a given frame. For a pixel (x,y), the 
 * Y sample lives at raw[2 * (y * width + x)]. 
 *
 * With grid_rows/grid_cols set, each cell is the mean luminance over its block; with them 
 * zero, one site per pixel. A site is emitted when its value is strictly above cfg.threshold.
 *
 * support: flattened index into the output geometry (rows x cols).
 * signal: value normalized to [0, 1] for each corresponding index in support.  
 */
static uint32_t yuyv_to_sparse(const v4l2_ctx_t* ctx, const uint8_t* __restrict__ raw, frame_t* out)
{
  if ( !ctx || !raw ) {
    return 0; 
  }

  const grid_info_t gi  = v4l2_info_from_ctx(ctx); 
  uint32_t count = 0; 

  for ( uint_fast32_t r = 0; r < gi.rows; r++ ) {
    // fast32 to hint ignore if downcast is slower; not required to be 32 
    const uint_fast32_t y0 = (uint_fast32_t)(((uint64_t)r * ctx->height) / gi.rows); 
    const uint_fast32_t y1 = (uint_fast32_t)(((uint64_t)(r + 1) * ctx->height) / gi.rows); 

    for ( uint_fast32_t c = 0; c < gi.cols; c++ ) {
      const uint_fast32_t x0 = (uint_fast32_t)(((uint64_t)c * ctx->width) / gi.cols);
      const uint_fast32_t x1 = (uint_fast32_t)(((uint64_t)(c + 1) * ctx->width) / gi.cols);

      uint64_t sum = 0; 
      uint32_t n   = 0; 

      for ( uint_fast32_t y = y0; y < y1; y++) {
        const uint8_t* row = raw + (size_t)2 * y * ctx->width; 
        for ( uint32_t x = x0; x < x1; x++ ) {
          sum += row[(size_t)x * 2]; 
          n++; 
        }
      } 

      if ( n == 0 ) {
        continue; 
      }

      const float value = (float)sum / (float)n; 

      // computed luminance is under the configured threshold and can be discarded 
      if ( value <= gi.thr ) {
        continue; 
      }

      if ( count >= out->capacity ) {
        fprintf(stderr, "v4l2: sparse overflow at %u sites\n", count); 
        return count; 
      }

      out->support[count] = (uint32_t)(r * gi.cols + c); 
      out->signal[count]  = value / 255.0f;               // normalize to [0, 1]  
      count++; 
    }
  }

  return count; 
}


static int v4l2_acquire(void* impl_ctx, frame_t* out)
{
  v4l2_ctx_t* ctx = (v4l2_ctx_t *)impl_ctx; 

  if ( !ctx || !out || ctx->fd < 0 ) {
    return -1; 
  }

  struct pollfd pfd = {
    .fd = ctx->fd, 
    .events = POLLIN
  };
  const int timeout = ctx->cfg.timeout_ms? (int)ctx->cfg.timeout_ms : 2000; 

  // Poll file description until the returned value is >= 0 or timeout hit  
  int pr; 
  do {
    pr = poll(&pfd, 1, timeout); 
  } while ( pr < 0 && errno == EINTR );

  // timeout was hit; per camera_interface_t contract  
  if ( pr == 0 ) {
    return 1; 
  }

  if ( pr < 0 ) {
    fprintf(stderr, "v4l2: poll failed: %s\n", strerror(errno)); 
    return -2; 
  }

  struct v4l2_buffer buf = {0}; 

  buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
  buf.memory = V4L2_MEMORY_MMAP; 

  if ( xioctl(ctx->fd, VIDIOC_DQBUF, &buf) < 0 ) {
    if ( errno == EAGAIN ) {
      return 1; 
    }

    fprintf(stderr, "v4l2: DQBUF failed: %s\n", strerror(errno)); 
    return -3; 
  }

  if ( buf.index >= ctx->n_buffers ) {
    fprintf(stderr, "v4l2: DQBUF returned out of bounds index %u\n", buf.index); 
    return -4; 
  }

  // WARN, not ERROR for no MONOTONIC reference 
  if ( !ctx->stamp_checked ) {
    uint32_t src = buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK; 

    if ( src != V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC ) {
      fprintf(stderr, "v4l2: driver timestamp source is 0x%x, not MONOTONIC\n", src); 
    }
    ctx->stamp_checked = true; 
  }
  
  const uint8_t* raw = (uint8_t *)ctx->buffers[buf.index].start; 
  out->count    = yuyv_to_sparse(ctx, raw, out);
  out->t_ns     = (int64_t)buf.timestamp.tv_sec * NANOSECONDS + (int64_t)buf.timestamp.tv_usec * 1000; 
  out->sequence = buf.sequence; 

  if ( ctx->debug_frame ) {
    memcpy(ctx->debug_frame, raw, (size_t)ctx->width * (size_t)ctx->height * 2); 
    ctx->debug_valid = true; 
  }

  ctx->frame_id++; 

  if ( xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0 ) {
    fprintf(stderr, "v4l2: QBUF failed: %s\n", strerror(errno)); 
    return -5; 
  }

  return 0; 
} 


const camera_interface_t v4l2_camera_interface = {
  .init    = v4l2_init,
  .deinit  = v4l2_deinit, 
  .acquire = v4l2_acquire 
};
