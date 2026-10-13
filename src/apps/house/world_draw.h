#pragma once
// Drawing the virtual household (world_logic.h) for the X4's screens: a room, or a whole floor of the home plan, one
// pixel at a time through pixel(x, y, ink), as pet_logic.h's drawLook. Tiles are 16 px (the art's own size) or 8 px
// (every other pixel, for big rooms and the whole house). No Arduino code, so it can be checked on a computer.
//
// The foundation's simple version: floor pattern, walls with door gaps, furniture (not yet turned), clutter. Pets are
// drawn by the screens over this (petDraw), at the scene's tile positions.
#include "home_art.h"
#include "world_logic.h"

namespace world {

// One sprite at (x0, y0), `tile` px a side: ink drawn, paper (when `paper`) drawn white, else left as it is.
template <typename Pixel> void drawSprite(uint16_t sprite, int x0, int y0, int tile, bool paper, Pixel pixel) {
  const int step = 16 / tile;
  for (int y = 0; y < tile; y++)
    for (int x = 0; x < tile; x++) {
      const bool ink = homeart::ink(sprite, x * step, y * step);
      if (ink || paper) pixel(x0 + x, y0 + y, ink);
    }
}

inline bool isDoor(const plan::Room &r, int tx, int ty) {
  for (const plan::Door &d : r.doors) {
    if (d.side == plan::NORTH && ty == 0 && tx == d.at) return true;
    if (d.side == plan::SOUTH && ty == r.h - 1 && tx == d.at) return true;
    if (d.side == plan::WEST && tx == 0 && ty == d.at) return true;
    if (d.side == plan::EAST && tx == r.w - 1 && ty == d.at) return true;
  }
  return false;
}

// A room, its top left at (x0, y0): r.w x r.h tiles of `tile` px. Walls are its edge tiles (a door is a gap).
template <typename Pixel>
void drawRoom(const plan::Room &r, const RoomState &s, int tile, int x0, int y0, Pixel pixel) {
  const uint16_t floor = homeart::FLOOR[r.type < plan::ROOM_TYPES ? r.type : plan::OTHER];
  for (int ty = 0; ty < r.h; ty++)
    for (int tx = 0; tx < r.w; tx++) {
      const bool wall = (tx == 0 || ty == 0 || tx == r.w - 1 || ty == r.h - 1) && !isDoor(r, tx, ty);
      drawSprite(wall ? homeart::WALL : floor, x0 + tx * tile, y0 + ty * tile, tile, true, pixel);
    }
  for (const plan::Piece &p : r.pieces) {
    const homeart::Furniture *f = p.id ? homeart::furniture(p.id) : nullptr;
    if (!f) continue;
    for (int dy = 0; dy < f->h; dy++)
      for (int dx = 0; dx < f->w; dx++)
        if (p.x + dx < r.w && p.y + dy < r.h)
          drawSprite(f->sprite + dy * f->w + dx, x0 + (p.x + dx) * tile, y0 + (p.y + dy) * tile, tile, true, pixel);
  }
  for (int i = 0; i < s.count; i++)
    if (const homeart::ClutterArt *c = homeart::clutterArt(s.clutter[i].kind))
      drawSprite(c->sprite, x0 + s.clutter[i].x * tile, y0 + s.clutter[i].y * tile, tile, false, pixel);
}

// Every room on one floor of the plan, the plan's tile grid at `tile` px from (x0, y0).
template <typename Pixel> void drawFloor(const Scene &s, int floor, int tile, int x0, int y0, Pixel pixel) {
  for (int i = 0; i < s.count; i++)
    if (s.rooms[i].floor == floor)
      drawRoom(s.rooms[i], s.state[i], tile, x0 + s.rooms[i].x * tile, y0 + s.rooms[i].y * tile, pixel);
}

}  // namespace world
