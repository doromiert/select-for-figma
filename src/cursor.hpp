#pragma once
#include <gdk/gdk.h>
#include "include/internal/cef_types_wrappers.h"

// Owns the CEF bitmap and generates correctly scaled GTK cursor textures.
GdkCursor* make_custom_cursor(const CefCursorInfo& info);
