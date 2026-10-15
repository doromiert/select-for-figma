#pragma once
#include <adwaita.h>
#include <memory>
#include <string>
#include "include/cef_frame.h"
#include "include/cef_v8.h"
#include "include/cef_process_message.h"

class Browser;
class Window;
class Zane {
public:
  explicit Zane(Browser& browser);
  ~Zane();
  void ensure(CefRefPtr<CefFrame> frame);
  void key(guint key, GdkModifierType modifiers);
  void commit(const char* text);
  void install(CefRefPtr<CefFrame> frame);
  bool receive(CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message);
  static bool receive_account(Browser& browser, CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message);
  static void refresh_account(Browser& browser);
  static void bind_renderer(CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
  void stop();
  void refresh();
  static void add_preferences(Window& window, AdwPreferencesPage* page);
private:
  struct Session;
  Browser& browser_;
  std::shared_ptr<Session> session_;
};
