#pragma once
#include <adwaita.h>
#include <epoxy/gl.h>
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_permission_handler.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Window;
class Zane;

class Browser final : public CefClient,
                      public CefLifeSpanHandler,
                      public CefRenderHandler,
                      public CefDisplayHandler,
                      public CefLoadHandler,
                      public CefRequestHandler,
                      public CefDownloadHandler,
                      public CefContextMenuHandler,
                      public CefJSDialogHandler,
                      public CefPermissionHandler {
public:
  explicit Browser(std::string url, bool software = false);
  GtkWidget* widget() const { return overlay_; }
  GtkWidget* surface() const { return area_; }
  std::string url;
  std::string name;
  std::string group_id;
  GdkPaintable* thumbnail();
  GdkTexture* account_frame() const;
  bool loading = false;
  bool closing = false;
  bool loaded = false;
  bool gpu_frames = false;
  bool custom_cursor_seen = false;
  bool is_auth_popup = false;
  bool is_home = false;
  void start();
  void navigate(const std::string& address);
  void reload();
  void back();
  void forward();
  void close();
  void set_visible(bool visible);
  void set_dark(bool dark);
  void set_permissions();
  void refresh_zane();
  bool canvas_input(const std::string& operation, double x, double y, double dx, double dy, const std::string& shortcut);
  void detach_callbacks();
  bool can_back() const { return browser_ && browser_->CanGoBack(); }
  bool can_forward() const { return browser_ && browser_->CanGoForward(); }
  CefRefPtr<CefBrowser> cef_browser() const { return browser_; }
  Window* owner() const;
  std::function<void()> on_changed;
  std::function<void()> on_closed;
  std::function<void()> on_close_cancelled;
  static unsigned active_count;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefProcessId, CefRefPtr<CefProcessMessage>) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t prompt_id, const CefString& origin,
      uint32_t permissions, CefRefPtr<CefPermissionPromptCallback> callback) override;
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override;
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width, int height) override;
  void OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const CefAcceleratedPaintInfo& info) override;
  void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override { popup_rect_ = rect; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser>) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& target_url,
      const CefString&, cef_window_open_disposition_t, bool user_gesture, const CefPopupFeatures&,
      CefWindowInfo& window_info, CefRefPtr<CefClient>& client, CefBrowserSettings&,
      CefRefPtr<CefDictionaryValue>&, bool*) override;
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const CefString& address) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool is_loading, bool, bool) override;
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override;
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode error,
      const CefString& text, const CefString& failed_url) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fullscreen) override;
  bool OnTooltip(CefRefPtr<CefBrowser>, CefString& text) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int) override;
  bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle, cef_cursor_type_t type, const CefCursorInfo&) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
      bool user_gesture, bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& target_url,
      cef_window_open_disposition_t, bool user_gesture) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
      const CefString& suggested_name, CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
      CefRefPtr<CefDownloadItemCallback>) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override;
  bool RunContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
      CefRefPtr<CefMenuModel> model, CefRefPtr<CefRunContextMenuCallback> callback) override;
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType type,
      const CefString& message, const CefString& default_prompt, CefRefPtr<CefJSDialogCallback> callback,
      bool& suppress) override;
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool,
      CefRefPtr<CefJSDialogCallback> callback) override;
private:
  ~Browser() override;
  CefRefPtr<CefBrowser> browser_;
  std::unique_ptr<Zane> zane_;
  GtkWidget* overlay_;
  GtkWidget* area_;
  GtkWidget* error_page_;
  GtkIMContext* im_;
  bool software_;
  bool creating_ = false;
  int browser_id_ = 0;
  std::string pending_navigation_;
  bool owns(CefRefPtr<CefBrowser> source) const;
  bool popup_visible_ = false;
  bool auth_has_document_ = false;
  bool auth_return_pending_ = false;
  CefRect popup_rect_;
  guint thumbnail_idle_ = 0;
  GdkPaintable* thumbnail_source_ = nullptr;
  GdkPaintable* thumbnail_ = nullptr;
  GLuint texture_ = 0, popup_texture_ = 0, program_ = 0, vao_ = 0;
  int texture_width_ = 0, texture_height_ = 0;
  bool texture_bgra_ = false, popup_bgra_ = false;
  GBytes* account_pixels_ = nullptr;
  int account_width_ = 0, account_height_ = 0;
  double mouse_x_ = 0, mouse_y_ = 0;
  int drag_button_ = 0;
  void setup_input();
  void changed();
  bool gl_ready();
  void init_gl();
  void clear_gl();
  void render();
  void upload(PaintElementType type, const void* pixels, int width, int height);
  void show_error(const std::string& title, const std::string& description);
  CefMouseEvent mouse(GdkModifierType modifiers = static_cast<GdkModifierType>(0));
  void send_key(guint keyval, guint keycode, GdkModifierType state, bool release);
  bool route(const std::string& target, bool user_gesture, bool new_tab);
  void schedule_auth_return(const std::string& completed);
  IMPLEMENT_REFCOUNTING(Browser);
};
