#pragma once
#include <gio/gio.h>

// A loopback-only implementation of Figma's local font-helper protocol.
// The wire format follows ReSketch's FontHelper.cpp (GPL-3.0-only).
class Fonts {
public:
  Fonts();
  ~Fonts();
  Fonts(const Fonts&) = delete;
  Fonts& operator=(const Fonts&) = delete;
  bool available() const { return available_; }
private:
  GSocketService* service_ = nullptr;
  bool available_ = false;
};
