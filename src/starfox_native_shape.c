#include "starfox_native_shape.h"

#include <stdbool.h>
#include <string.h>

enum {
  kEndShape = 0,
  kPoints8 = 4,
  kPoints16 = 8,
  kEndPoints = 12,
  kFaces = 20,
  kFrames = 28,
  kJump = 32,
  kBsp = 40,
  kVisibilities = 48,
  kPointsX16 = 52,
  kPointsX8 = 56,
  kBspInit = 60,
  kBspEnd = 64,
  kBspExit = 68,
  kQuit = 72,
  kSprite = 80,
  kSpriteVisibility = 84,
  kFaceEndQuit = 0xff,
  kFaceEndContinue = 0xfe,

  kMaxVertices = 256,
  kMaxEdges = 768,
  kMaxVisitedBsp = 256,
};

typedef struct Vec3i {
  int32_t x;
  int32_t y;
  int32_t z;
} Vec3i;

typedef struct ScreenPoint {
  int32_t x;
  int32_t y;
  int32_t z;
  bool visible;
} ScreenPoint;

typedef struct Edge {
  uint8_t a;
  uint8_t b;
} Edge;

typedef struct ShapeHeader {
  uint32_t points_address;
  uint32_t faces_address;
  uint8_t shift;
} ShapeHeader;

typedef struct DecodeContext {
  const uint8_t *rom;
  size_t rom_size;
  Vec3i vertices[kMaxVertices];
  unsigned vertex_count;
  Edge edges[kMaxEdges];
  unsigned edge_count;
  uint32_t visited_bsp[kMaxVisitedBsp];
  unsigned visited_bsp_count;
} DecodeContext;

static bool lorom_offset(uint32_t address, size_t *offset_out) {
  const uint16_t low = (uint16_t)address;
  if (low < 0x8000)
    return false;
  *offset_out = (size_t)((address >> 16) & 0x7f) * 0x8000u +
                (size_t)(low & 0x7fff);
  return true;
}

static bool rom_u8(const DecodeContext *ctx, uint32_t address, uint8_t *out) {
  size_t offset;
  if (!ctx || !lorom_offset(address, &offset) || offset >= ctx->rom_size)
    return false;
  *out = ctx->rom[offset];
  return true;
}

static bool rom_u16(const DecodeContext *ctx, uint32_t address, uint16_t *out) {
  uint8_t lo, hi;
  if (!rom_u8(ctx, address, &lo) || !rom_u8(ctx, address + 1u, &hi))
    return false;
  *out = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

static bool rom_i16(const DecodeContext *ctx, uint32_t address, int16_t *out) {
  uint16_t value;
  if (!rom_u16(ctx, address, &value))
    return false;
  *out = (int16_t)value;
  return true;
}

static int8_t s8(uint8_t value) {
  return (int8_t)value;
}

static bool relative_target(const DecodeContext *ctx, uint32_t relative_address,
                            uint32_t *target_out) {
  int16_t relative;
  (void)ctx;
  if (!rom_i16(ctx, relative_address, &relative))
    return false;
  *target_out = (uint32_t)((int64_t)relative_address + 1 + relative);
  return true;
}

static bool decode_header(DecodeContext *ctx, uint16_t shape_address,
                          ShapeHeader *header) {
  uint16_t points_pointer, faces_pointer;
  uint8_t data_bank, shift;
  const uint32_t address = shape_address;
  if (!rom_u16(ctx, address, &points_pointer) ||
      !rom_u8(ctx, address + 2u, &data_bank) ||
      !rom_u16(ctx, address + 3u, &faces_pointer) ||
      !rom_u8(ctx, address + 7u, &shift))
    return false;
  if (points_pointer == 0 || faces_pointer == 0 || shift > 15)
    return false;
  header->points_address = ((uint32_t)data_bank << 16) | points_pointer;
  header->faces_address = ((uint32_t)data_bank << 16) | faces_pointer;
  header->shift = shift;
  return true;
}

static bool append_vertex(DecodeContext *ctx, int32_t x, int32_t y, int32_t z) {
  if (ctx->vertex_count >= kMaxVertices)
    return false;
  ctx->vertices[ctx->vertex_count++] = (Vec3i){x, y, z};
  return true;
}

static bool decode_points(DecodeContext *ctx, const ShapeHeader *header,
                          uint32_t animation_frame) {
  uint32_t cursor = header->points_address;
  for (unsigned commands = 0; commands < 16384; commands++) {
    uint8_t opcode, count;
    if (!rom_u8(ctx, cursor++, &opcode))
      return false;
    if (opcode == kEndPoints || opcode == kEndShape)
      return true;
    if (opcode == kFrames) {
      uint8_t frame_count;
      uint32_t entry, target;
      if (!rom_u8(ctx, cursor++, &frame_count) || frame_count == 0)
        return false;
      entry = cursor + (animation_frame % frame_count) * 2u;
      if (!relative_target(ctx, entry, &target))
        return false;
      cursor = target;
      continue;
    }
    if (opcode == kJump) {
      uint32_t target;
      if (!relative_target(ctx, cursor, &target))
        return false;
      cursor = target;
      continue;
    }
    if (opcode != kPoints8 && opcode != kPoints16 &&
        opcode != kPointsX8 && opcode != kPointsX16)
      return false;
    if (!rom_u8(ctx, cursor++, &count))
      return false;
    for (unsigned i = 0; i < count; i++) {
      int32_t x, y, z;
      if (opcode == kPoints16 || opcode == kPointsX16) {
        int16_t wx, wy, wz;
        if (!rom_i16(ctx, cursor, &wx) || !rom_i16(ctx, cursor + 2u, &wy) ||
            !rom_i16(ctx, cursor + 4u, &wz))
          return false;
        cursor += 6u;
        x = wx;
        y = wy;
        z = wz;
      } else {
        uint8_t bx, by, bz;
        if (!rom_u8(ctx, cursor, &bx) || !rom_u8(ctx, cursor + 1u, &by) ||
            !rom_u8(ctx, cursor + 2u, &bz))
          return false;
        cursor += 3u;
        x = s8(bx);
        y = s8(by);
        z = s8(bz);
      }
      if (!append_vertex(ctx, x, y, z))
        return false;
      if ((opcode == kPointsX8 || opcode == kPointsX16) &&
          !append_vertex(ctx, -x, y, z))
        return false;
    }
  }
  return false;
}

static void append_edge(DecodeContext *ctx, uint8_t a, uint8_t b) {
  if (a >= ctx->vertex_count || b >= ctx->vertex_count || a == b ||
      ctx->edge_count >= kMaxEdges)
    return;
  for (unsigned i = 0; i < ctx->edge_count; i++) {
    const Edge edge = ctx->edges[i];
    if ((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a))
      return;
  }
  ctx->edges[ctx->edge_count++] = (Edge){a, b};
}

static bool decode_face_record(DecodeContext *ctx, uint32_t *cursor,
                               uint8_t vertex_count) {
  uint8_t previous = 0;
  uint8_t first = 0;
  uint8_t ignored;
  if (!rom_u8(ctx, *cursor + 4u, &ignored))
    return false;
  *cursor += 5u;
  for (uint8_t i = 0; i < vertex_count; i++) {
    uint8_t index;
    if (!rom_u8(ctx, *cursor, &index))
      return false;
    *cursor += 1u;
    if (i == 0) {
      first = index;
    } else {
      append_edge(ctx, previous, index);
    }
    previous = index;
  }
  if (vertex_count > 2)
    append_edge(ctx, previous, first);
  return true;
}

static bool decode_face_list(DecodeContext *ctx, uint32_t address) {
  uint8_t opcode;
  uint32_t cursor = address;
  if (!rom_u8(ctx, cursor++, &opcode) || opcode != kFaces)
    return false;
  for (unsigned faces = 0; faces < 4096; faces++) {
    if (!rom_u8(ctx, cursor++, &opcode))
      return false;
    if (opcode == kFaceEndQuit || opcode == kFaceEndContinue)
      return true;
    if (opcode < 2 || opcode > 12)
      return false;
    if (!decode_face_record(ctx, &cursor, opcode))
      return false;
  }
  return false;
}

static bool bsp_seen(DecodeContext *ctx, uint32_t address) {
  for (unsigned i = 0; i < ctx->visited_bsp_count; i++) {
    if (ctx->visited_bsp[i] == address)
      return true;
  }
  if (ctx->visited_bsp_count >= kMaxVisitedBsp)
    return true;
  ctx->visited_bsp[ctx->visited_bsp_count++] = address;
  return false;
}

static bool decode_bsp(DecodeContext *ctx, uint32_t address) {
  uint8_t opcode;
  if (bsp_seen(ctx, address))
    return true;
  if (!rom_u8(ctx, address, &opcode))
    return false;
  if (opcode == kBsp) {
    uint32_t face_address, alternate = 0;
    uint8_t alternate_offset_raw;
    int8_t alternate_offset;
    if (!relative_target(ctx, address + 2u, &face_address) ||
        !rom_u8(ctx, address + 4u, &alternate_offset_raw))
      return false;
    alternate_offset = s8(alternate_offset_raw);
    if (alternate_offset)
      alternate = (uint32_t)((int64_t)address + 4 + alternate_offset);
    (void)decode_face_list(ctx, face_address);
    if (!decode_bsp(ctx, address + 5u))
      return false;
    if (alternate && !decode_bsp(ctx, alternate))
      return false;
    return true;
  }
  if (opcode == kBspExit) {
    uint32_t face_address;
    if (!relative_target(ctx, address + 1u, &face_address))
      return false;
    (void)decode_face_list(ctx, face_address);
    return true;
  }
  return opcode == kBspEnd || opcode == kEndShape || opcode == kQuit;
}

static bool decode_faces(DecodeContext *ctx, const ShapeHeader *header) {
  uint32_t cursor = header->faces_address;
  bool in_faces = false;
  for (unsigned commands = 0; commands < 65536; commands++) {
    uint8_t opcode;
    if (!rom_u8(ctx, cursor++, &opcode))
      return false;
    if (in_faces && opcode >= 2 && opcode <= 12) {
      if (!decode_face_record(ctx, &cursor, opcode))
        return false;
      continue;
    }
    if (opcode == kEndShape || opcode == kQuit || opcode == kFaceEndQuit)
      return true;
    if (opcode == kFaceEndContinue) {
      in_faces = false;
      continue;
    }
    if (opcode == kVisibilities) {
      uint8_t count;
      if (!rom_u8(ctx, cursor++, &count))
        return false;
      cursor += (uint32_t)count * 3u;
      continue;
    }
    if (opcode == kFaces) {
      in_faces = true;
      continue;
    }
    if (opcode == kBspInit)
      return decode_bsp(ctx, cursor);
    if (opcode == kBspEnd)
      continue;
    if (opcode == kSprite) {
      cursor += 3u;
      continue;
    }
    if (opcode == kSpriteVisibility) {
      cursor += 4u;
      continue;
    }
    return false;
  }
  return false;
}

static int32_t clamp_i32(int64_t value, int32_t lo, int32_t hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return (int32_t)value;
}

static int32_t sin_q15(uint16_t angle) {
  const uint8_t phase = (uint8_t)(angle >> 8);
  const unsigned quadrant = phase >> 6;
  int32_t ramp = phase & 0x3f;
  if (quadrant == 1 || quadrant == 3)
    ramp = 63 - ramp;
  int32_t value = (ramp * 32767) / 63;
  if (quadrant >= 2)
    value = -value;
  return value;
}

static int32_t cos_q15(uint16_t angle) {
  return sin_q15((uint16_t)(angle + 0x4000u));
}

static int32_t q15_mul(int32_t a, int32_t b) {
  return (int32_t)(((int64_t)a * b) >> 15);
}

static Vec3i rotate_point(Vec3i value, const StarFoxNativeShapePose *pose) {
  int32_t s = sin_q15(pose->pitch), c = cos_q15(pose->pitch);
  int32_t y = q15_mul(value.y, c) - q15_mul(value.z, s);
  int32_t z = q15_mul(value.y, s) + q15_mul(value.z, c);
  value.y = y;
  value.z = z;

  s = sin_q15(pose->yaw);
  c = cos_q15(pose->yaw);
  int32_t x = q15_mul(value.x, c) + q15_mul(value.z, s);
  z = -q15_mul(value.x, s) + q15_mul(value.z, c);
  value.x = x;
  value.z = z;

  s = sin_q15(pose->roll);
  c = cos_q15(pose->roll);
  x = q15_mul(value.x, c) - q15_mul(value.y, s);
  y = q15_mul(value.x, s) + q15_mul(value.y, c);
  value.x = x + pose->x;
  value.y = y + pose->y;
  value.z = value.z + pose->z;
  return value;
}

static bool project_point(Vec3i point, const StarFoxNativeShapePose *pose,
                          int width, int height, ScreenPoint *out) {
  if (point.z < 16)
    return false;
  const int64_t px = (int64_t)point.x * 256 / point.z;
  const int64_t py = (int64_t)point.y * 256 / point.z;
  out->x = (int32_t)pose->widescreen_extra + 16 + pose->vanish_x +
           clamp_i32(px, -16384, 16383);
  out->y = pose->vanish_y + clamp_i32(py, -16384, 16383);
  out->z = point.z;
  out->visible = out->x > -512 && out->x < width + 512 &&
                 out->y > -512 && out->y < height + 512;
  return true;
}

static void put_bgra(uint8_t *pixels, size_t pitch, int width, int height,
                     int x, int y, uint8_t b, uint8_t g, uint8_t r) {
  if (!pixels || x < 0 || y < 0 || x >= width || y >= height)
    return;
  uint8_t *p = pixels + (size_t)y * pitch + (size_t)x * 4u;
  p[0] = b;
  p[1] = g;
  p[2] = r;
  p[3] = 0xff;
}

static void draw_line(uint8_t *pixels, size_t pitch, int width, int height,
                      const StarFoxNativeShapePose *pose,
                      ScreenPoint a, ScreenPoint b) {
  int x0 = a.x, y0 = a.y;
  const int x1 = b.x, y1 = b.y;
  const int dx = x0 < x1 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = y0 < y1 ? y1 - y0 : y0 - y1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = (dx > dy ? dx : -dy) / 2;
  for (unsigned guard = 0; guard < 4096; guard++) {
    if (x0 < pose->protect_center_left || x0 >= pose->protect_center_right) {
      put_bgra(pixels, pitch, width, height, x0, y0, 0x20, 0xff, 0xa8);
    }
    if (x0 == x1 && y0 == y1)
      break;
    const int e2 = err;
    if (e2 > -dx) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dy) {
      err += dx;
      y0 += sy;
    }
  }
}

int StarFoxNativeDrawShapeWireframe(const uint8_t *rom, size_t rom_size,
                                    uint16_t shape_address,
                                    const StarFoxNativeShapePose *pose,
                                    uint8_t *pixels, size_t pitch,
                                    int width, int height,
                                    StarFoxNativeShapeStats *stats) {
  DecodeContext ctx;
  ShapeHeader header;
  Vec3i transformed[kMaxVertices];
  ScreenPoint projected[kMaxVertices];
  if (stats)
    memset(stats, 0, sizeof(*stats));
  if (!rom || !rom_size || !pose || !pixels || width <= 0 || height <= 0)
    return 0;
  memset(&ctx, 0, sizeof(ctx));
  ctx.rom = rom;
  ctx.rom_size = rom_size;
  if (!decode_header(&ctx, shape_address, &header) ||
      !decode_points(&ctx, &header, pose->animation_frame) ||
      !decode_faces(&ctx, &header))
    return 0;

  const int32_t scale = 1 << header.shift;
  for (unsigned i = 0; i < ctx.vertex_count; i++) {
    Vec3i point = ctx.vertices[i];
    point.x = clamp_i32((int64_t)point.x * scale, -32768, 32767);
    point.y = clamp_i32((int64_t)point.y * scale, -32768, 32767);
    point.z = clamp_i32((int64_t)point.z * scale, -32768, 32767);
    transformed[i] = rotate_point(point, pose);
    projected[i].visible =
        project_point(transformed[i], pose, width, height, &projected[i]);
  }

  unsigned lines = 0;
  for (unsigned i = 0; i < ctx.edge_count; i++) {
    const Edge edge = ctx.edges[i];
    Vec3i a = transformed[edge.a];
    Vec3i b = transformed[edge.b];
    ScreenPoint pa, pb;
    if (a.z < 16 && b.z < 16)
      continue;
    if (a.z < 16 || b.z < 16) {
      Vec3i *near_point = a.z < 16 ? &a : &b;
      const Vec3i far_point = a.z < 16 ? b : a;
      const int32_t dz = far_point.z - near_point->z;
      if (dz == 0)
        continue;
      near_point->x += (int32_t)(((int64_t)far_point.x - near_point->x) *
                                 (16 - near_point->z) / dz);
      near_point->y += (int32_t)(((int64_t)far_point.y - near_point->y) *
                                 (16 - near_point->z) / dz);
      near_point->z = 16;
    }
    if (!project_point(a, pose, width, height, &pa) ||
        !project_point(b, pose, width, height, &pb))
      continue;
    if (!pa.visible || !pb.visible)
      continue;
    draw_line(pixels, pitch, width, height, pose, pa, pb);
    lines++;
  }
  if (stats) {
    stats->vertices = ctx.vertex_count;
    stats->faces = ctx.edge_count;
    stats->lines = lines;
  }
  return lines != 0;
}
