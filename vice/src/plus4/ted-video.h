/* TED video output between horizontal-counter events. */
#ifndef VICE_TED_VIDEO_H
#define VICE_TED_VIDEO_H

#include "types.h"

struct snapshot_module_s;
struct raster_s;

void ted_video_reset(CLOCK clk);
void ted_video_update(CLOCK clk);
void ted_video_end_line(CLOCK clk);
void ted_video_draw_line(struct raster_s *raster);
int ted_video_snapshot_write(struct snapshot_module_s *m);
int ted_video_snapshot_read(struct snapshot_module_s *m);

#endif
