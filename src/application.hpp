#pragma once
#include <adwaita.h>
#include "browser.hpp"
#include "state.hpp"
#include "keepassxc.hpp"
#include "include/cef_request_context.h"
#include <memory>
#include <vector>

class Fonts;
class Application;
class WorkspaceTabs;
class Window {
public:
  explicit Window(Application& app, bool zane_account = false);
  ~Window();
  Application& app;
  GtkWindow* window;
  AdwTabView* tabs;
  AdwTabOverview* overview;
  AdwToastOverlay* toasts;
  std::unique_ptr<WorkspaceTabs> workspace;
  bool closing = false;
  bool destroyed = false;
  const bool zane_account;
  CefRefPtr<Browser> current() const;
  CefRefPtr<Browser> open(const std::string& url, bool activate = true, bool defer = false);
  CefRefPtr<Browser> popup(const std::string& url);
  void update();
  void browser_closed(Browser* browser);
  void finish_auth(Browser* browser);
  void open_link_dialog();
  void toast(const std::string& message);
  void show_downloads();
  void show_preferences();
  void pair_keepassxc(GtkWidget* row = nullptr);
  void fill_keepassxc();
  AdwDialog* show_login_picker(CefRefPtr<Browser> browser, const std::string& source, std::vector<KeePassXC::Login> entries);
  void show_about();
  void show_shortcuts();
  void toggle_fullscreen();
  void native_home();
  void refresh_home();
  void persist();
  void close();
private:
  GtkWidget* home_button_ = nullptr;
  GtkWidget* reload_ = nullptr;
  GtkWidget* toolbar_ = nullptr;
  GtkWidget* tab_bar_ = nullptr;
  GtkWidget* header_content_ = nullptr;
  GtkWidget* content_ = nullptr;
  GtkWidget* root_ = nullptr;
  AdwWindowTitle* title_ = nullptr;
  GtkWidget* home_ = nullptr;
  GdkPaintable* capture_ = nullptr;
  CefRefPtr<Browser> home_browser_;
  CefRefPtr<Browser> auth_browser_;
  bool adding_page_ = false;
  bool finishing_tab_close_ = false;
  bool refresh_after_auth_ = true;
  std::vector<CefRefPtr<Browser>> browsers_;
  AdwTabPage* context_page_ = nullptr;
  void add_actions();
  void attach(CefRefPtr<Browser> browser, bool activate);
  void rebuild_home();
  friend class Application;
  friend class WorkspaceTabs;
};

class Application {
public:
  Application();
  ~Application();
  AdwApplication* gtk_app;
  State state;
  bool software = false;
  bool cef_ready = false;
  bool smoke_test = false;
  bool render_test = false;
  bool auth_test = false;
  bool diagnostic_failed = false;
  std::string screenshot;
  guint save_source = 0;
  std::unique_ptr<Fonts> fonts;
  std::vector<std::unique_ptr<Window>> windows;
  Window* new_window(bool present = true);
  Window* zane_account_window(bool present = true);
  CefRefPtr<CefRequestContext> zane_account_context;
  Window* authentication_window = nullptr;
  Window* active_window();
  void open(const std::string& url);
  void persist();
  void queue_save();
  bool group_in_use(const std::string& id) const;
  void prune_groups();
  void apply_appearance();
  void external(const std::string& url);
  void export_completed(const std::string& path, const std::string& source, const std::string& title);
  void take_screenshot(Window& window, const std::string& filename);
  void take_screenshot(GtkWindow* window, const std::string& filename);
  int run(int argc, char** argv);
};
