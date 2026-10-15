#include "cursor.hpp"
#include <cairo.h>
#include <algorithm>
#include <cmath>

namespace {
struct Image {
  GBytes* pixels;
  int pixel_width, pixel_height, width, height, hotspot_x, hotspot_y;
  ~Image() { g_bytes_unref(pixels); }
};
GdkTexture* texture(GdkCursor*, int, double scale, int* width, int* height, int* x, int* y, gpointer data) {
  auto* image = static_cast<Image*>(data);
  if (!std::isfinite(scale) || scale <= 0 || scale > 16) return nullptr;
  *width = image->width; *height = image->height; *x = image->hotspot_x; *y = image->hotspot_y;
  const int w = std::max(1, static_cast<int>(std::lround(*width * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(*height * scale)));
  if (w > 2048 || h > 2048) return nullptr;
  if (w == image->pixel_width && h == image->pixel_height)
    return gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, image->pixels, w * 4);
  auto* source = cairo_image_surface_create_for_data(
    const_cast<unsigned char*>(static_cast<const unsigned char*>(g_bytes_get_data(image->pixels, nullptr))),
    CAIRO_FORMAT_ARGB32, image->pixel_width, image->pixel_height, image->pixel_width * 4);
  auto* target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t* cr = cairo_create(target);
  cairo_scale(cr, static_cast<double>(w) / image->pixel_width, static_cast<double>(h) / image->pixel_height);
  cairo_set_source_surface(cr, source, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr); cairo_destroy(cr); cairo_surface_flush(target);
  const int stride = cairo_image_surface_get_stride(target);
  GBytes* pixels = g_bytes_new(cairo_image_surface_get_data(target), stride * h);
  GdkTexture* result = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, pixels, stride);
  g_bytes_unref(pixels); cairo_surface_destroy(source); cairo_surface_destroy(target); return result;
}
}
GdkCursor* make_custom_cursor(const CefCursorInfo& info) {
  const double scale = info.image_scale_factor;
  if (!info.buffer || info.size.width <= 0 || info.size.height <= 0 || info.size.width > 2048 || info.size.height > 2048 ||
      !std::isfinite(scale) || scale <= 0 || scale > 16) return nullptr;
  const int width = std::max(1, static_cast<int>(std::lround(info.size.width / scale)));
  const int height = std::max(1, static_cast<int>(std::lround(info.size.height / scale)));
  auto* image = new Image{g_bytes_new(info.buffer, static_cast<size_t>(info.size.width) * info.size.height * 4),
    info.size.width, info.size.height, width, height,
    std::clamp(static_cast<int>(std::lround(info.hotspot.x / scale)), 0, width - 1),
    std::clamp(static_cast<int>(std::lround(info.hotspot.y / scale)), 0, height - 1)};
  GdkCursor* fallback = gdk_cursor_new_from_name("default", nullptr);
  GdkCursor* cursor = gdk_cursor_new_from_callback(texture, image, +[](gpointer p) { delete static_cast<Image*>(p); }, fallback);
  g_object_unref(fallback); return cursor;
}
