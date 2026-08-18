/*
 * v4l2_webcam.h  Andrew Belles 
 */
#ifndef V4L2_WEBCAM_H
#define V4L2_WEBCAM_H 

#include <stdint.h> 
#include "camera.h" 

typedef struct {
  const char* device;  // e.g. "/dev/video0"
  uint32_t width; 
  uint32_t height; 
  uint32_t grid_rows;  // 0 = no downsample  
  uint32_t grid_cols; 
  uint32_t n_buffers;  // mmap buffers to request >= 2 
  uint32_t timeout_ms; // per-frame poll timeout, 0 defaults to 2000 
  uint8_t threshold;   // luminance strictly above this is a site 
} v4l2_config_t; 

typedef struct v4l2_ctx v4l2_ctx_t; 

v4l2_ctx_t* v4l2_ctx_create(const v4l2_config_t* cfg); 
void        v4l2_ctx_destroy(v4l2_ctx_t* ctx); 

void v4l2_ctx_get_format(const v4l2_ctx_t* ctx, uint32_t* width, uint32_t* height,
                         uint32_t* pixelformat, uint32_t* n_buffers); 


/*
 * Number of sites a frame from this context can hold in the worst case. 
 */ 
size_t v4l2_max_sites(const v4l2_ctx_t* ctx); 

/*
 * Writes the most recent raw YUYV buffer as a P5 PGM (luminance plane). 
 *
 * Currently a stub/not-implemented
 */
// void v4l2_dump_luminance(const v4l2_ctx_t* ctx, const char* path); 

extern const camera_interface_t v4l2_camera_interface; 

#endif // !V4L2_WEBCAM_H
