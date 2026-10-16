/*
METAL_REPORT.H

Reports of what the Metal renderer cannot do, in two families kept apart so
that a report says whose problem it is:

  compatibility ("Metal compatibility:") - the game asked for an Xbox state,
    format or resource this renderer does not translate: the D3D8 front end
    (xbox_compat/, shaders/) is incomplete, often after an upstream change;
  failure ("Metal failure:") - Metal itself failed at native GPU work: a
    shader that did not compile, a pipeline that did not link, a command
    buffer that faulted (reported when it completes, after the frame).

Up to 128 distinct problems (kind, operation, state, value) are logged once,
with where each was first met (frame and draw) and what is drawn instead.
Operations are identified by their first 47 bytes. Repeats are counted; after
the table fills, unitemized occurrences are counted without claiming unique
identities. All occurrence counters saturate at UINT32_MAX and are logged at exit. With debug.metal_strict
(HALO_METAL_STRICT) the first report stops the game, for test runs that
must not pass with a silent fallback.

Shared by the front end (Xbox ABI, whose `long` is 32 bits) and the Metal
backend (Objective-C, the host's): only fixed-width types cross, and callers
format their own detail text.
*/

#ifndef HALO_METAL_REPORT_H
#define HALO_METAL_REPORT_H

#include <stdint.h>
#include <stdatomic.h>

enum metal_report_kind
{
	/* compatibility: the Xbox interface */
	METAL_REPORT_UNSUPPORTED_STATE,
	METAL_REPORT_UNSUPPORTED_FORMAT,
	METAL_REPORT_INVALID_RESOURCE,
	/* failure: Metal */
	METAL_REPORT_SHADER,
	METAL_REPORT_PIPELINE,
	METAL_REPORT_COMMAND,
	METAL_REPORT_KINDS
};

struct metal_report
{
	enum metal_report_kind kind;
	/* what was being done: "D3DRS_ZFUNC", "texture format", "pixel shader" */
	const char *operation;
	/* the state's index, a texture stage, or 0; and the offending value */
	uint32_t state;
	uint32_t value;
	/* what is drawn instead ("drawn as NEVER"), or NULL */
	const char *fallback;
	/* free text (Metal's error description), or NULL */
	const char *detail;
	const char *file;
	int line;
};

/* Approximate diagnostic context, not a coherent snapshot or synchronization
barrier. The device advances these (xbox_device.c). */
extern _Atomic(uint64_t) metal_report_frame;
extern _Atomic(uint32_t) metal_report_draw;

/* settings: whether the first report stops the game */
void metal_report_configure(int strict);
/* log the report the first time it is seen; 1 if it was new */
int metal_report_issue(const struct metal_report *report);
/* occurrences of a kind so far (tests, the regression harness) */
uint32_t metal_report_count(enum metal_report_kind kind);
/* The number of itemized distinct problems, capped at 128 (not overflow occurrences). */
uint32_t metal_report_distinct(void);
/* log the counts of every problem seen (at exit) */
void metal_report_summary(void);
/* Forget everything (tests). Call only after all reporting threads have joined. */
void metal_report_reset(void);

#define METAL_REPORT(kind_, operation_, state_, value_, fallback_, detail_) \
	metal_report_issue(&(struct metal_report){ (kind_), (operation_), (uint32_t)(state_), (uint32_t)(value_), \
		(fallback_), (detail_), __FILE__, __LINE__ })

#endif
