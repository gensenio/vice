/* Timing tests substitute the video output, as they do raster_line_emulate.
   The separate video test exercises the production pipeline. */
void ted_video_update(CLOCK clk)
{
    ted.video_clk = clk;
}

void ted_video_draw_line(raster_t *raster)
{
}

void ted_video_end_line(CLOCK clk)
{
    ted.video_clk = ted.video_line_clk = clk;
    ted.video_changed = 0;
}
