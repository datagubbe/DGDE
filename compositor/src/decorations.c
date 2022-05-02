#include "decorations.h"
#include <stdint.h>

const uint32_t DECORATION_SIZE[4] = {24, 7, 7, 7};

static void draw_titlebar(const struct wlr_box *window,
                          struct wlr_renderer *renderer,
                          const float *projection, const float *colors[4]) {

  const float *base = colors[0];
  const float *dark = colors[2];
  const float *light = colors[3];

  const struct wlr_box b = {
      .height = DECORATION_SIZE[0] - DECORATION_SIZE[1],
      .width = window->width - (DECORATION_SIZE[1] + DECORATION_SIZE[3]),
      .x = window->x + DECORATION_SIZE[3],
      .y = window->y + DECORATION_SIZE[1],
  };

  struct wlr_box b1 = b;

  wlr_render_rect(renderer, &b, base, projection);

  // "shadows"
  b1.height = 2;
  wlr_render_rect(renderer, &b1, light, projection);
  b1.y = b.y + b.height - 2;
  wlr_render_rect(renderer, &b1, dark, projection);

  b1 = b;
  b1.width = 2;
  wlr_render_rect(renderer, &b1, light, projection);
  b1.x = b.x + b.width - 2;
  wlr_render_rect(renderer, &b1, dark, projection);
}

static void draw_borders(const struct wlr_box *window,
                         struct wlr_renderer *renderer, const float *projection,
                         const float *colors[4]) {
  const float *base = colors[0];
  const float *dark = colors[2];
  const float *light = colors[3];

  // top border
  struct wlr_box b;

  for (uint32_t start = window->y; start < window->y + window->height;
       start += window->height - DECORATION_SIZE[2]) {
    b.y = start;
    b.x = window->x;
    b.width = window->width;
    b.height = 1;
    wlr_render_rect(renderer, &b, dark, projection);

    b.y += 1;
    b.height = 2;
    wlr_render_rect(renderer, &b, light, projection);

    b.y += 2;
    wlr_render_rect(renderer, &b, base, projection);

    b.y += 2;
    wlr_render_rect(renderer, &b, dark, projection);
  }

  // left + right  border
  uint32_t start = window->x;
  b.x = start;
  b.y = window->y + 1;
  b.width = 1;
  b.height = window->height - 2;
  wlr_render_rect(renderer, &b, dark, projection);

  b.width = 2;

  b.y += 2;
  b.height -= 4;
  b.x += 1;
  wlr_render_rect(renderer, &b, light, projection);

  b.height -= 2;
  b.y += 2;
  b.x += 2;
  wlr_render_rect(renderer, &b, base, projection);

  b.height -= 6;
  b.y += 2;
  b.x += 2;
  wlr_render_rect(renderer, &b, dark, projection);

  // right border
  start += window->width - DECORATION_SIZE[1];
  b.x = start;
  b.y = window->y + 7;
  b.width = 1;
  b.height = window->height - 13;
  wlr_render_rect(renderer, &b, dark, projection);

  b.width = 2;

  b.y -= 2;
  b.height += 2;
  b.x += 1;
  wlr_render_rect(renderer, &b, light, projection);

  b.height += 4;
  b.y -= 2;
  b.x += 2;
  wlr_render_rect(renderer, &b, base, projection);

  b.height += 6;
  b.y -= 2;
  b.x += 2;
  wlr_render_rect(renderer, &b, dark, projection);
}

void decorate_window(const struct wlr_box *window,
                     struct wlr_renderer *renderer, const float *projection,
                     const float *colors[4]) {
  draw_borders(window, renderer, projection, colors);
  draw_titlebar(window, renderer, projection, colors);
}
