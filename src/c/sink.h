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
#include <stddef.h>

#define SINK_MAX_CAMERAS 16

/*
 * Fallback bounds, applied to any camera with no registered spec.
 */
typedef struct sink_check {
  double expect_period_ms;
  double tol;
  uint32_t max_sites;
  bool verbose;
} sink_check_t;

/*
 * Per-camera expectation. 
 *
 * max_sites 0 leaves that camera's index check disabled;
 * expect_period_ms 0 leaves its cadence check disabled.
 */
typedef struct sink_cam_spec {
  uint32_t camera_id;
  uint32_t max_sites;
  double expect_period_ms;
} sink_cam_spec_t;

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
  uint32_t unexpected_cams; // ids that sent frames but were never configured
} sink_stats_t; 

typedef struct sink sink_t; 

sink_t* sink_new(const char* endpoint, const sink_check_t* check);
void    sink_del(sink_t* sink);

/*
 * Registers what each camera_id is expected to produce, replacing any previous
 * table. Also grows the decode buffer to the largest spec.
 *
 * A camera_id with no spec falls back to the sink_check_t bounds, so a caller
 * that never calls this behaves exactly as before.
 *
 * Returns:
 * - 0 on success 
 * - -1 on bad arguments or allocation failure.
 */
int sink_expect(sink_t* sink, const sink_cam_spec_t* specs, size_t n);

/*
 * Waits up to timeout_ms for one message and validates it.
 *
 * Not thread safe; one thread per sink. Mirrors camera_tick's constraint, so a
 * fan-in of N camera threads still drains from a single consumer thread.
 *
 * Returns: 
 * - 0 for message handled (valid or not, metadata stored in counters)
 * - 1 on timeout 
 * - negative on hard error 
 */ 
int sink_recv(sink_t* sink, int timeout_ms); 

void sink_get_stats(const sink_t* sink, sink_stats_t* out);

int sink_report(const sink_t* sink); 

#endif // !SINK_H
