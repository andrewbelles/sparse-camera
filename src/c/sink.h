/*
 * sink.h  Andrew Belles 
 *
 * Current Fan-in consumer. Bins one ZMQ_PULL. Every camera connects a PUSH to it. 
 * Each message is checked against the invariants the pipeline is supposed to guarentee, per camera_id. 
 *
 * Invariants:
 * - message parses as a frame (format, version, length vs count)
 * - t_ns strictly increases 
 * - inter-frame delta stays within tolerance of an expected period 
 * - sequence is contiguous (gap implies driver dropped frames)
 * - support indices land inside [0, max_sites)
 * - signal values are finite and inside [0, 1]
 *
 * Violations are counted/logged. 
 */
#ifndef SINK_H
#define SINK_H 

#include <stdint.h> 
#include <stdbool.h>

#define SINK_MAX_CAMERAS 16

typedef struct sink_check {
  double expect_period_ms; 
  double tol; 
  uint32_t max_sites; 
  bool verbose; 
} sink_check_t; 

typedef struct sink_stats {
  uint64_t received; 
  uint64_t bad_parse; // format, version, length 
  uint64_t backwards; // t_ns did not increase 
  uint64_t jitter;    // delta outside tolerance 
  uint64_t seq_gaps;  // number of gap events 
  uint64_t seq_lost;  // total frames missing across above gaps 
  uint64_t bad_index; // support out of range 
  uint64_t bad_value; // value outside normalized [0, 1]
  uint32_t cameras_seen; 
} sink_stats_t; 

typedef struct sink sink_t; 

sink_t* sink_new(const char* endpoint, const sink_check_t* check); 
void    sink_del(sink_t* sink); 

/*
 * Waits up to timeout_ms for one message and validates it. 
 * Returns: 
 * - 0 for message handled (valid or not, metadata stored in counters)
 * - 1 on timeout 
 * - negative on hard error 
 */ 
int sink_recv(sink_t* sink, int timeout_ms); 

void sink_get_stats(const sink_t* sink, sink_stats_t* out);

int sink_report(const sink_t* sink); 

#endif // !SINK_H
