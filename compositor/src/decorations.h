#ifndef DECORATIONS_H
#define DECORATIONS_H

#include <stdint.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>

void decorate_window(const struct wlr_box *window,
                     struct wlr_renderer *renderer, const float *projection,
                     const float *colors[4]);

extern const uint32_t DECORATION_SIZE[4];

#endif
