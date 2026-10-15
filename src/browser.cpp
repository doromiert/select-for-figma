#include "browser.hpp"
#include "application.hpp"
#include "policy.hpp"
#include "clipboard.hpp"
#include "cursor.hpp"
#include "zane.hpp"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include <epoxy/egl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

unsigned Browser::active_count = 0;
namespace {
uint32_t modifiers(GdkModifierType state) {
  uint32_t result = 0;
  if (state & GDK_SHIFT_MASK) result |= EVENTFLAG_SHIFT_DOWN;
  if (state & GDK_CONTROL_MASK) result |= EVENTFLAG_CONTROL_DOWN;
  if (state & GDK_ALT_MASK) result |= EVENTFLAG_ALT_DOWN;
  if (state & GDK_SUPER_MASK) result |= EVENTFLAG_COMMAND_DOWN;
  if (state & GDK_LOCK_MASK) result |= EVENTFLAG_CAPS_LOCK_ON;
  if (state & GDK_BUTTON1_MASK) result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  if (state & GDK_BUTTON2_MASK) result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
  if (state & GDK_BUTTON3_MASK) result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  return result;
}
int windows_key(guint key) {
  if (key >= GDK_KEY_a && key <= GDK_KEY_z) return key - GDK_KEY_a + 'A';
  if (key >= GDK_KEY_A && key <= GDK_KEY_Z) return key;
  if (key >= GDK_KEY_0 && key <= GDK_KEY_9) return key;
  if (key >= GDK_KEY_F1 && key <= GDK_KEY_F24) return 0x70 + key - GDK_KEY_F1;
  switch (key) {
    case GDK_KEY_BackSpace: return 8; case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: return 9;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: return 13;
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: return 16;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R: return 17;
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: return 18;
    case GDK_KEY_Escape: return 27; case GDK_KEY_space: return 32;
    case GDK_KEY_Page_Up: return 33; case GDK_KEY_Page_Down: return 34;
    case GDK_KEY_End: return 35; case GDK_KEY_Home: return 36;
    case GDK_KEY_Left: return 37; case GDK_KEY_Up: return 38;
    case GDK_KEY_Right: return 39; case GDK_KEY_Down: return 40;
    case GDK_KEY_Insert: return 45; case GDK_KEY_Delete: return 46;
    case GDK_KEY_Super_L: return 91; case GDK_KEY_Super_R: return 92;
    case GDK_KEY_semicolon: case GDK_KEY_colon: return 186;
    case GDK_KEY_equal: case GDK_KEY_plus: return 187;
    case GDK_KEY_comma: case GDK_KEY_less: return 188;
    case GDK_KEY_minus: case GDK_KEY_underscore: return 189;
    case GDK_KEY_period: case GDK_KEY_greater: return 190;
    case GDK_KEY_slash: case GDK_KEY_question: return 191;
    case GDK_KEY_grave: case GDK_KEY_asciitilde: return 192;
    case GDK_KEY_bracketleft: case GDK_KEY_braceleft: return 219;
    case GDK_KEY_backslash: case GDK_KEY_bar: return 220;
    case GDK_KEY_bracketright: case GDK_KEY_braceright: return 221;
    case GDK_KEY_apostrophe: case GDK_KEY_quotedbl: return 222;
    default: return gdk_keyval_to_unicode(key);
  }
}
GLuint shader(GLenum type, const char* source) {
  GLuint id = glCreateShader(type); glShaderSource(id, 1, &source, nullptr); glCompileShader(id);
  GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(id, sizeof log, nullptr, log); g_warning("Browser shader: %s", log); }
  return id;
}
}

Browser::Browser(std::string address, bool software) : url(std::move(address)), software_(software) {
  name = Policy::title(url);
  overlay_ = gtk_overlay_new(); g_object_ref_sink(overlay_);
  gtk_widget_set_hexpand(overlay_, true); gtk_widget_set_vexpand(overlay_, true);
  g_object_set_data(G_OBJECT(overlay_), "browser", this);
  for (const auto* signal : {"notify::dark", "notify::accent-color"})
    g_signal_connect(adw_style_manager_get_default(), signal, G_CALLBACK(+[](AdwStyleManager*, GParamSpec*, Browser* self) {
      // Named GTK colors update after the style manager emits its notification.
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer p) -> gboolean {
        (*static_cast<CefRefPtr<Browser>*>(p))->refresh_theme(); return G_SOURCE_REMOVE;
      }, new CefRefPtr<Browser>(self), +[](gpointer p) { delete static_cast<CefRefPtr<Browser>*>(p); });
    }), this);
  area_ = gtk_gl_area_new();
  gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area_), GDK_GL_API_GLES);
  gtk_gl_area_set_required_version(GTK_GL_AREA(area_), 3, 0);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(area_), false);
  gtk_widget_set_focusable(area_, true);
  gtk_widget_set_hexpand(area_, true); gtk_widget_set_vexpand(area_, true);
  gtk_overlay_set_child(GTK_OVERLAY(overlay_), area_);
  error_page_ = adw_status_page_new();
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(error_page_), "network-offline-symbolic");
  GtkWidget* retry = gtk_button_new_with_label("Try Again");
  gtk_widget_add_css_class(retry, "suggested-action");
  gtk_widget_set_halign(retry, GTK_ALIGN_CENTER);
  adw_status_page_set_child(ADW_STATUS_PAGE(error_page_), retry);
  g_signal_connect_swapped(retry, "clicked", G_CALLBACK(+[](Browser* self) { self->reload(); }), this);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay_), error_page_);
  gtk_widget_set_visible(error_page_, false);
  g_signal_connect(area_, "realize", G_CALLBACK(+[](GtkGLArea*, Browser* self) { self->init_gl(); }), this);
  g_signal_connect(area_, "unrealize", G_CALLBACK(+[](GtkGLArea*, Browser* self) { self->clear_gl(); }), this);
  g_signal_connect(area_, "render", G_CALLBACK(+[](GtkGLArea*, GdkGLContext*, Browser* self) -> gboolean { self->render(); return true; }), this);
  g_signal_connect(area_, "resize", G_CALLBACK(+[](GtkGLArea*, int, int, Browser* self) {
    if (self->browser_) self->browser_->GetHost()->WasResized();
  }), this);
  g_signal_connect(area_, "notify::scale-factor", G_CALLBACK(+[](GObject*, GParamSpec*, Browser* self) {
    if (self->browser_) { self->browser_->GetHost()->NotifyScreenInfoChanged(); self->browser_->GetHost()->WasResized(); }
  }), this);
  g_signal_connect(area_, "map", G_CALLBACK(+[](GtkWidget*, Browser* self) {
    if (!self->loaded) self->start();
    self->set_visible(true);
  }), this);
  g_signal_connect(area_, "unmap", G_CALLBACK(+[](GtkWidget*, Browser* self) { self->set_visible(false); }), this);
  thumbnail_source_ = gtk_widget_paintable_new(overlay_);
  g_signal_connect(thumbnail_source_, "invalidate-contents", G_CALLBACK(+[](GdkPaintable* source, Browser* self) {
    if (!gtk_widget_get_mapped(self->overlay_) || self->closing) return;
    if (self->thumbnail_idle_) return;
    // Capturing synchronously from invalidate-contents can re-enter GTK's
    // GLArea snapshot while its backing frame is being replaced. Cache only
    // after that render has completed, not inside its invalidation callback.
    self->thumbnail_idle_ = g_idle_add_full(G_PRIORITY_LOW,+[](gpointer p) -> gboolean {
      auto* self = static_cast<Browser*>(p); self->thumbnail_idle_ = 0;
      if (!gtk_widget_get_mapped(self->overlay_) || self->closing) return G_SOURCE_REMOVE;
      GdkPaintable* image = gdk_paintable_get_current_image(self->thumbnail_source_);
      if (gdk_paintable_get_intrinsic_width(image) > 0 && gdk_paintable_get_intrinsic_height(image) > 0) g_set_object(&self->thumbnail_,image);
      g_object_unref(image); return G_SOURCE_REMOVE;
    },self,nullptr);
  }), this);
  setup_input();
}
Browser::~Browser() {
  g_signal_handlers_disconnect_by_data(adw_style_manager_get_default(), this);
  g_clear_pointer(&account_pixels_,g_bytes_unref);
  if (thumbnail_idle_) g_source_remove(thumbnail_idle_);
  // A focused widget can emit controller signals while being removed. Detach
  // callbacks before releasing the IM context or the Browser they refer to.
  GListModel* controllers = gtk_widget_observe_controllers(area_);
  for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
    auto* controller = G_OBJECT(g_list_model_get_item(controllers, i));
    g_signal_handlers_disconnect_by_data(controller, this); g_object_unref(controller);
  }
  g_object_unref(controllers);
  g_signal_handlers_disconnect_by_data(area_, this);
  g_signal_handlers_disconnect_by_data(adw_status_page_get_child(ADW_STATUS_PAGE(error_page_)), this);
  g_signal_handlers_disconnect_by_data(thumbnail_source_, this);
  g_clear_object(&thumbnail_source_); g_clear_object(&thumbnail_); clear_gl();
  g_object_set_data(G_OBJECT(overlay_), "browser", nullptr);
  gtk_im_context_set_client_widget(im_, nullptr);
  g_object_unref(overlay_); g_object_unref(im_);
}
Window* Browser::owner() const {
  GtkRoot* root = gtk_widget_get_root(overlay_);
  return root ? static_cast<Window*>(g_object_get_data(G_OBJECT(root), "window")) : nullptr;
}
void Browser::changed() { if (on_changed) on_changed(); else if (owner()) owner()->update(); }
void Browser::refresh_zane() { if (zane_) zane_->refresh(); }
void Browser::start() {
  if (loaded || creating_ || browser_ || closing || (owner() && (owner()->closing || owner()->destroyed))) return;
  if (owner() && owner()->app.smoke_test && Policy::is_figma(url)) return;
  if (is_home && !is_auth_popup && owner() && (owner()->app.smoke_test || owner()->app.render_test || owner()->app.auth_test) &&
      !(owner()->app.auth_test && url.rfind("http://127.0.0.1:", 0) == 0)) return;
  loaded = true; creating_ = true;
  CefWindowInfo info; info.SetAsWindowless(0);
  info.shared_texture_enabled = !software_ && !(owner() && owner()->zane_account);
  CefBrowserSettings settings;
  settings.windowless_frame_rate = owner() && owner()->zane_account ? 15 : 60;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  auto context = owner() && owner()->zane_account ? owner()->app.zane_account_context : nullptr;
  if (!CefBrowserHost::CreateBrowser(info, this, "", settings, nullptr, context)) {
    creating_ = false; loaded = false; show_error("Could Not Start Browser", "Close this tab and try opening the file again.");
  }
}
void Browser::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (browser_id_) {
    // A client belongs to exactly one native tab and one Chromium browser.
    // Reject unexpected extra creations without replacing its live browser.
    if (browser_id_ != browser->GetIdentifier()) browser->GetHost()->CloseBrowser(true);
    return;
  }
  const bool requested = creating_; creating_ = false;
  browser_ = browser; browser_id_ = browser->GetIdentifier(); loaded = true; ++active_count;
  if (owner() && owner()->app.auth_test) g_print("Local browser created: popup=%s; Home=%s\n", is_auth_popup ? "yes" : "no", is_home ? "yes" : "no");
  if (owner()) g_application_hold(G_APPLICATION(owner()->app.gtk_app));
  if (closing) { browser_->GetHost()->CloseBrowser(true); return; }
  browser_->GetHost()->WasResized();
  set_visible(gtk_widget_get_mapped(area_));
  refresh_theme();
  set_permissions();
  // Native creations start blank: load only the latest requested address once
  // creation finishes. Popups retain Chromium's opener and initial navigation.
  if (requested || !pending_navigation_.empty()) {
    pending_navigation_.clear(); browser_->GetMainFrame()->LoadURL(url);
  }
  changed();
}
bool Browser::owns(CefRefPtr<CefBrowser> source) const { return source && browser_id_ && source->GetIdentifier() == browser_id_; }
void Browser::OnBeforeClose(CefRefPtr<CefBrowser> source) {
  if (!owns(source)) return;
  CefRefPtr<Browser> keep_alive = this;
  Window* win = owner();
  browser_ = nullptr; browser_id_ = 0; loaded = false; creating_ = false; --active_count;
  if (on_closed) on_closed(); else if (win) win->browser_closed(this);
  if (win) g_application_release(G_APPLICATION(win->app.gtk_app));
}
void Browser::detach_callbacks() { on_changed = nullptr; on_closed = nullptr; on_close_cancelled = nullptr; }
void Browser::close() {
  if (closing) return;
  if (zane_) zane_->stop();
  gtk_im_context_focus_out(im_); gtk_im_context_set_client_widget(im_, nullptr);
  closing = true;
  if (browser_) browser_->GetHost()->CloseBrowser(false);
  else if (!loaded) { if (on_closed) on_closed(); else if (owner()) owner()->browser_closed(this); }
}
void Browser::navigate(const std::string& address) {
  url = address; gtk_widget_set_visible(error_page_, false);
  if (browser_) browser_->GetMainFrame()->LoadURL(address);
  else if (loaded || creating_) pending_navigation_ = address;
  else start();
}
void Browser::reload() {
  gtk_widget_set_visible(error_page_, false);
  if (browser_) browser_->Reload(); else if (loaded || creating_) pending_navigation_ = url; else start();
}
void Browser::back() { if (browser_) browser_->GoBack(); }
void Browser::forward() { if (browser_) browser_->GoForward(); }
void Browser::set_visible(bool visible) {
  if (owner() && owner()->zane_account) visible=true;
  if (browser_) { browser_->GetHost()->WasHidden(!visible); if (visible) browser_->GetHost()->Invalidate(PET_VIEW); }
  if (browser_ && owner() && owner()->zane_account) browser_->GetHost()->WasResized();
}
void Browser::refresh_theme() {
  auto* win = owner();
  if (!browser_ || !win || closing) return;
  const bool fixture = win->app.render_test && g_getenv("FIGMA_GTK_TEST_EDITOR_THEME") && url.starts_with("data:text/html;base64,");
  if (!Policy::is_figma(url) && !fixture) return;
  const bool enabled = win->app.state.adwaita_editor && !Policy::is_sign_in(url);
  const bool home = !Policy::is_file(url) && !fixture;
  std::string css;
  if (enabled) {
    const bool dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    const auto color = [&](const char* name, const char* fallback) {
      GdkRGBA rgba; gdk_rgba_parse(&rgba, fallback);
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_style_context_lookup_color(gtk_widget_get_style_context(widget()), name, &rgba);
      G_GNUC_END_IGNORE_DEPRECATIONS
      char* value = gdk_rgba_to_string(&rgba); std::string result(value); g_free(value); return result;
    };
    css = std::string("html[data-select-adwaita]{color-scheme:")+(dark ? "dark;" : "light;");
    css += "--select-bg:"+color("window_bg_color", dark ? "#242424" : "#fafafa")+";";
    css += "--select-sidebar:"+color("sidebar_bg_color", dark ? "#303030" : "#ebebeb")+";";
    css += "--select-toolbar:"+color("headerbar_bg_color", dark ? "#303030" : "#ffffff")+";";
    css += "--select-popover:"+color("popover_bg_color", dark ? "#383838" : "#ffffff")+";";
    css += "--select-fg:"+color("window_fg_color", dark ? "#ffffff" : "#2e3436")+";";
    css += "--select-toggle-bg:"+color("active_toggle_bg_color", dark ? "#66666b" : "#ffffff")+";";
    css += "--select-toggle-fg:"+color("active_toggle_fg_color", dark ? "#ffffff" : "#2e3436")+";";
    const std::string accent_bg = color("accent_bg_color", "#3584e4");
    GdkRGBA accent, standalone;
    gdk_rgba_parse(&accent, accent_bg.c_str()); adw_rgba_to_standalone(&accent, dark, &standalone);
    char* accent_css = gdk_rgba_to_string(&standalone);
    css += std::string("--select-accent:")+accent_css+";"; g_free(accent_css);
    css += "--select-accent-bg:"+accent_bg+";";
    css += "--select-accent-fg:"+color("accent_fg_color", "#ffffff")+";";
    css += "--select-font:'Adwaita Sans',Cantarell,system-ui,sans-serif;}\n";
    auto* bytes = g_resources_lookup_data("/io/github/doromiert/FigmaGtk/theme/adwaita.css", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes) return;
    gsize size; const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size)); css.append(data, size); g_bytes_unref(bytes);
  }
  auto* node = json_node_new(JSON_NODE_VALUE); json_node_set_string(node, css.c_str());
  char* quoted = json_to_string(node, false); json_node_free(node);
  const std::string script = std::string(R"JS((() => {
    const css = )JS")+quoted+R"JS(;
    const root = document.documentElement;
    if (!root) return;
    let style = document.getElementById('select-adwaita-theme');
    if (!css) { style?.remove(); root.removeAttribute('data-select-adwaita'); root.removeAttribute('data-select-adwaita-home'); return; }
    if (!style) {
      style = document.createElement('style'); style.id = 'select-adwaita-theme';
      const nonce = document.querySelector('style[nonce], script[nonce]')?.nonce;
      if (nonce) style.nonce = nonce;
      (document.head || root).append(style);
    }
    style.textContent = css;
    root.setAttribute('data-select-adwaita', '');
    root.toggleAttribute('data-select-adwaita-home', )JS"+(home ? "true" : "false")+R"JS();
  })())JS";
  g_free(quoted);
  browser_->GetMainFrame()->ExecuteJavaScript(script, "figma-gtk://theme", 1);
}
GdkPaintable* Browser::thumbnail() {
  // GTK retains the last full-resolution render node, including its GL texture.
  // Snapshotting it is constant-time; no pixel readback or downsampling occurs
  // on the tab activation path. It also preserves the previous hidden page.
  if (thumbnail_) return GDK_PAINTABLE(g_object_ref(thumbnail_));
  return nullptr;
}
void Browser::set_permissions() {
  auto* win = owner();
  if (!browser_ || !win) return;
  const bool fixture = win->app.auth_test && g_getenv("FIGMA_GTK_TEST_NOTIFICATIONS") && url.rfind("http://127.0.0.1:", 0) == 0;
  if (!Policy::is_figma(url) && !fixture) return;
  auto context = browser_->GetHost()->GetRequestContext();
  context->SetContentSetting(url, url, CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS,
    win->app.state.notifications ? CEF_CONTENT_SETTING_VALUE_ALLOW : CEF_CONTENT_SETTING_VALUE_BLOCK);
  if (!fixture) context->SetContentSetting(url, url, CEF_CONTENT_SETTING_TYPE_LOOPBACK_NETWORK,
    win->app.state.local_fonts ? CEF_CONTENT_SETTING_VALUE_ALLOW : CEF_CONTENT_SETTING_VALUE_BLOCK);
}
void Browser::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  const bool background=owner() && owner()->zane_account && !gtk_widget_get_mapped(area_);
  rect = CefRect(0,0,background ? 1100 : std::max(1,gtk_widget_get_width(area_)),background ? 750 : std::max(1,gtk_widget_get_height(area_)));
}
bool Browser::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& info) {
  CefRect rect; GetViewRect(browser, rect);
  info.device_scale_factor = gtk_widget_get_scale_factor(area_);
  info.depth = 32; info.depth_per_component = 8; info.rect = info.available_rect = rect;
  return true;
}
bool Browser::gl_ready() {
  if (!gtk_widget_get_realized(area_)) return false;
  gtk_gl_area_make_current(GTK_GL_AREA(area_));
  return !gtk_gl_area_get_error(GTK_GL_AREA(area_));
}
void Browser::init_gl() {
  if (!gl_ready()) {
    const GError* error = gtk_gl_area_get_error(GTK_GL_AREA(area_));
    show_error("Graphics Not Available", error ? error->message : "An OpenGL ES 3 context is required."); return;
  }
  const char* vs = "#version 300 es\nprecision highp float; out vec2 uv; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); uv=vec2(p.x,1.0-p.y); gl_Position=vec4(p*2.0-1.0,0,1);}";
  const char* fs = "#version 300 es\nprecision highp float; in vec2 uv; uniform sampler2D frame; uniform bool bgra; out vec4 color; void main(){vec4 c=texture(frame,uv);color=bgra?c.bgra:c;}";
  GLuint vertex = shader(GL_VERTEX_SHADER, vs), fragment = shader(GL_FRAGMENT_SHADER, fs);
  program_ = glCreateProgram(); glAttachShader(program_, vertex); glAttachShader(program_, fragment);
  glLinkProgram(program_); glDeleteShader(vertex); glDeleteShader(fragment);
  glGenVertexArrays(1, &vao_);
  if (browser_) browser_->GetHost()->Invalidate(PET_VIEW);
}
void Browser::clear_gl() {
  if (gl_ready()) {
    if (texture_) glDeleteTextures(1, &texture_);
    if (popup_texture_) glDeleteTextures(1, &popup_texture_);
    if (program_) glDeleteProgram(program_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
  }
  texture_ = popup_texture_ = program_ = vao_ = 0;
  texture_width_ = texture_height_ = 0;
}
void Browser::render() {
  if (!gl_ready() || !program_) return;
  const GLenum toggles[] = {GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_BLEND,GL_RASTERIZER_DISCARD};
  GLboolean enabled[6], color_mask[4]; GLint old_viewport[4], old_program, old_vao, old_active, old_texture;
  for (size_t i=0;i<6;++i) { enabled[i] = glIsEnabled(toggles[i]); glDisable(toggles[i]); }
  glGetBooleanv(GL_COLOR_WRITEMASK,color_mask); glGetIntegerv(GL_VIEWPORT,old_viewport); glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao); glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active); glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_texture);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  const int scale = gtk_widget_get_scale_factor(area_);
  const int width = gtk_widget_get_width(area_) * scale, height = gtk_widget_get_height(area_) * scale;
  glViewport(0, 0, width, height);
  glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program_); glBindVertexArray(vao_); glActiveTexture(GL_TEXTURE0);
  glUniform1i(glGetUniformLocation(program_, "frame"), 0);
  if (texture_) {
    glBindTexture(GL_TEXTURE_2D, texture_); glUniform1i(glGetUniformLocation(program_, "bgra"), texture_bgra_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  if (popup_visible_ && popup_texture_) {
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(popup_rect_.x * scale, height - (popup_rect_.y + popup_rect_.height) * scale,
        popup_rect_.width * scale, popup_rect_.height * scale);
    glBindTexture(GL_TEXTURE_2D, popup_texture_); glUniform1i(glGetUniformLocation(program_, "bgra"), popup_bgra_);
    glDrawArrays(GL_TRIANGLES, 0, 3); glDisable(GL_BLEND);
  }
  glBindTexture(GL_TEXTURE_2D,old_texture); glActiveTexture(old_active); glBindVertexArray(old_vao); glUseProgram(old_program);
  glViewport(old_viewport[0],old_viewport[1],old_viewport[2],old_viewport[3]); glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
  for (size_t i=0;i<6;++i) { if (enabled[i]) glEnable(toggles[i]); else glDisable(toggles[i]); }
}
void Browser::upload(PaintElementType type, const void* pixels, int width, int height) {
  if (!gl_ready() || width <= 0 || height <= 0) return;
  GLuint& texture = type == PET_POPUP ? popup_texture_ : texture_;
  if (!texture) glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint unpack_buffer, row_length, skip_rows, skip_pixels, alignment;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer); glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip_pixels); glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glPixelStorei(GL_UNPACK_SKIP_ROWS,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0); glPixelStorei(GL_UNPACK_ALIGNMENT,4);
  if (type == PET_VIEW && width == texture_width_ && height == texture_height_)
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack_buffer); glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length); glPixelStorei(GL_UNPACK_SKIP_ROWS,skip_rows); glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip_pixels); glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
  if (type == PET_VIEW) { texture_width_ = width; texture_height_ = height; texture_bgra_ = true; }
  else popup_bgra_ = true;
  gtk_gl_area_queue_render(GTK_GL_AREA(area_));
}
void Browser::OnPaint(CefRefPtr<CefBrowser> source, PaintElementType type, const RectList&, const void* buffer, int width, int height) {
  if (!owns(source)) return;
  if (type==PET_VIEW && owner() && owner()->zane_account && width>0 && height>0 && width<=8192 && height<=8192) {
    g_clear_pointer(&account_pixels_,g_bytes_unref);
    account_pixels_=g_bytes_new(buffer,static_cast<gsize>(width)*height*4);
    account_width_=width;account_height_=height;
  }
  upload(type, buffer, width, height);
  if (type == PET_VIEW && owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_RENDER_STATE") && gl_ready()) {
    // Simulate state left by another GTK draw. The editor must still repaint
    // every pixel, and restore that state rather than relying on GL defaults.
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,0,0); glEnable(GL_RASTERIZER_DISCARD); glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
  }
}
GdkTexture* Browser::account_frame() const {
  if (!account_pixels_) return nullptr;
  return gdk_memory_texture_new(account_width_,account_height_,GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,account_pixels_,static_cast<gsize>(account_width_)*4);
}
void Browser::OnAcceleratedPaint(CefRefPtr<CefBrowser> source, PaintElementType type, const RectList&, const CefAcceleratedPaintInfo& info) {
  if (!owns(source)) return;
  if (!gl_ready()) return;
  const EGLDisplay display = eglGetCurrentDisplay();
  if (display == EGL_NO_DISPLAY || !epoxy_has_egl_extension(display, "EGL_EXT_image_dma_buf_import")) {
    show_error("Graphics Driver Not Supported", "GPU frame sharing is unavailable. Launch with --software-rendering to use the compatibility renderer."); return;
  }
  const int width = info.extra.coded_size.width, height = info.extra.coded_size.height;
  if (width <= 0 || height <= 0 || info.plane_count < 1 || info.plane_count > 4) return;
  // FourCC AR24 (BGRA bytes) / AB24 (RGBA bytes), little-endian.
  const EGLint format = info.format == CEF_COLOR_TYPE_BGRA_8888 ? 0x34325241 : 0x34324241;
  std::vector<EGLint> attrs = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_LINUX_DRM_FOURCC_EXT, format };
  const EGLint fd[] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
  const EGLint offset[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
  const EGLint pitch[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
  const EGLint low[] = {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
  const EGLint high[] = {EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
  const bool use_modifiers = epoxy_has_egl_extension(display, "EGL_EXT_image_dma_buf_import_modifiers");
  for (int i = 0; i < info.plane_count; ++i) {
    attrs.insert(attrs.end(), {fd[i], info.planes[i].fd, offset[i], static_cast<EGLint>(info.planes[i].offset), pitch[i], static_cast<EGLint>(info.planes[i].stride)});
    if (use_modifiers) attrs.insert(attrs.end(), {low[i], static_cast<EGLint>(info.modifier & 0xffffffff), high[i], static_cast<EGLint>(info.modifier >> 32)});
  }
  attrs.push_back(EGL_NONE);
  EGLImage image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attrs.data());
  if (image == EGL_NO_IMAGE_KHR) {
    show_error("Could Not Share GPU Frame", "Try the compatibility renderer with --software-rendering."); return;
  }
  GLint previous_read, previous_draw;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
  GLuint imported, read_fbo, write_fbo;
  glGenTextures(1, &imported); glBindTexture(GL_TEXTURE_2D, imported);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
  glGenFramebuffers(1, &read_fbo); glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, imported, 0);
  GLuint& texture = type == PET_POPUP ? popup_texture_ : texture_;
  if (!texture) glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (type == PET_POPUP || width != texture_width_ || height != texture_height_)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &write_fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, write_fbo);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
      glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    const bool clipped = glIsEnabled(GL_SCISSOR_TEST); glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (clipped) glEnable(GL_SCISSOR_TEST);
    // CEF reuses the source buffer once this callback returns. Finish the GPU copy first.
    glFinish();
    gpu_frames = true;
    if (type == PET_VIEW) { texture_width_ = width; texture_height_ = height; texture_bgra_ = false; }
    else popup_bgra_ = false;
    gtk_gl_area_queue_render(GTK_GL_AREA(area_));
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, previous_read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous_draw);
  glDeleteFramebuffers(1, &read_fbo); glDeleteFramebuffers(1, &write_fbo); glDeleteTextures(1, &imported);
  eglDestroyImageKHR(display, image);
}
void Browser::OnPopupShow(CefRefPtr<CefBrowser> source, bool show) {
  if (!owns(source)) return;
  popup_visible_ = show; gtk_gl_area_queue_render(GTK_GL_AREA(area_));
}
void Browser::show_error(const std::string& title, const std::string& description) {
  adw_status_page_set_title(ADW_STATUS_PAGE(error_page_), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(error_page_), description.c_str());
  gtk_widget_set_visible(error_page_, true);
}
void Browser::setup_input() {
  im_ = gtk_im_multicontext_new(); g_object_set_data(G_OBJECT(area_),"browser-im",im_); gtk_im_context_set_client_widget(im_, area_);
  g_signal_connect(im_, "commit", G_CALLBACK(+[](GtkIMContext*, const char* text, Browser* self) {
    if (self->zane_) self->zane_->commit(text);
    if (self->browser_) self->browser_->GetHost()->ImeCommitText(text, CefRange(UINT32_MAX, UINT32_MAX), 0);
  }), this);
  g_signal_connect(im_, "preedit-changed", G_CALLBACK(+[](GtkIMContext* im, Browser* self) {
    if (!self->browser_) return;
    char* text; int cursor; PangoAttrList* attrs;
    gtk_im_context_get_preedit_string(im, &text, &attrs, &cursor);
    std::u16string value = CefString(text).ToString16();
    CefCompositionUnderline underline; underline.range = CefRange(0, value.size());
    underline.color = CefColorSetARGB(255, 0, 0, 0); underline.thick = false;
    self->browser_->GetHost()->ImeSetComposition(text, {underline}, CefRange(UINT32_MAX, UINT32_MAX), CefRange(cursor, cursor));
    g_free(text); pango_attr_list_unref(attrs);
  }), this);
  GtkEventController* focus = gtk_event_controller_focus_new();
  g_signal_connect(focus, "enter", G_CALLBACK(+[](GtkEventControllerFocus*, Browser* self) {
    if (self->closing) return;
    gtk_im_context_set_client_widget(self->im_, self->area_);
    gtk_im_context_focus_in(self->im_); if (self->browser_) self->browser_->GetHost()->SetFocus(true);
  }), this);
  g_signal_connect(focus, "leave", G_CALLBACK(+[](GtkEventControllerFocus*, Browser* self) {
    gtk_im_context_focus_out(self->im_); if (self->browser_) self->browser_->GetHost()->SetFocus(false);
  }), this);
  gtk_widget_add_controller(area_, focus);
  GtkEventController* motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(+[](GtkEventControllerMotion* c, double x, double y, Browser* self) {
    self->mouse_x_ = x; self->mouse_y_ = y;
    if (self->browser_) self->browser_->GetHost()->SendMouseMoveEvent(self->mouse(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c))), false);
  }), this);
  g_signal_connect(motion, "leave", G_CALLBACK(+[](GtkEventControllerMotion* c, Browser* self) {
    if (self->browser_) self->browser_->GetHost()->SendMouseMoveEvent(self->mouse(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c))), true);
  }), this);
  gtk_widget_add_controller(area_, motion);
  GtkGesture* click = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
  g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick* c, int n, double x, double y, Browser* self) {
    gtk_widget_grab_focus(self->area_); self->mouse_x_ = x; self->mouse_y_ = y;
    const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(c)); self->drag_button_ = button;
    if (!self->browser_) return;
    if (button == 8) { self->back(); return; } if (button == 9) { self->forward(); return; }
    self->browser_->GetHost()->SendMouseClickEvent(self->mouse(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c))),
        button == 2 ? MBT_MIDDLE : button == 3 ? MBT_RIGHT : MBT_LEFT, false, std::min(n, 3));
  }), this);
  g_signal_connect(click, "released", G_CALLBACK(+[](GtkGestureClick* c, int n, double x, double y, Browser* self) {
    self->mouse_x_ = x; self->mouse_y_ = y; const int button = self->drag_button_; self->drag_button_ = 0;
    if (!self->browser_ || button > 3) return;
    self->browser_->GetHost()->SendMouseClickEvent(self->mouse(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c))),
        button == 2 ? MBT_MIDDLE : button == 3 ? MBT_RIGHT : MBT_LEFT, true, std::min(n, 3));
  }), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(click));
  GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll* c, double dx, double dy, Browser* self) -> gboolean {
    if (!self->browser_) return false;
    const double factor = gtk_event_controller_scroll_get_unit(c) == GDK_SCROLL_UNIT_SURFACE ? 1.0 : 40.0;
    self->browser_->GetHost()->SendMouseWheelEvent(self->mouse(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c))), std::lround(-dx * factor), std::lround(-dy * factor));
    return true;
  }), this);
  gtk_widget_add_controller(area_, scroll);
  GtkEventController* keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey* c, guint key, guint code, GdkModifierType state, Browser* self) -> gboolean {
    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) == GDK_CONTROL_MASK && gdk_keyval_to_lower(key) == GDK_KEY_t && self->owner()) {
      g_action_group_activate_action(G_ACTION_GROUP(self->owner()->window), "new-tab", nullptr); return true;
    }
    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) == GDK_CONTROL_MASK && (key == GDK_KEY_Page_Down || key == GDK_KEY_Page_Up) && self->owner()) {
      g_action_group_activate_action(G_ACTION_GROUP(self->owner()->window), key == GDK_KEY_Page_Down ? "next-tab" : "previous-tab", nullptr); return true;
    }
    if (((state & GDK_CONTROL_MASK) && gdk_keyval_to_lower(key) == GDK_KEY_v) || ((state & GDK_SHIFT_MASK) && key == GDK_KEY_Insert)) {
      Clipboard::paste(*self, (state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)); return true;
    }
    if (self->zane_) self->zane_->key(key,state);
    self->send_key(key, code, state, false);
    GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c));
    if (event) gtk_im_context_filter_keypress(self->im_, event);
    return true;
  }), this);
  g_signal_connect(keys, "key-released", G_CALLBACK(+[](GtkEventControllerKey* c, guint key, guint code, GdkModifierType state, Browser* self) {
    self->send_key(key, code, state, true);
    GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c));
    if (event) gtk_im_context_filter_keypress(self->im_, event);
  }), this);
  gtk_widget_add_controller(area_, keys);
}
CefMouseEvent Browser::mouse(GdkModifierType state) {
  CefMouseEvent e; e.x = std::lround(mouse_x_); e.y = std::lround(mouse_y_); e.modifiers = modifiers(state); return e;
}
void Browser::send_key(guint keyval, guint keycode, GdkModifierType state, bool release) {
  if (!browser_) return;
  CefKeyEvent e; e.type = release ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
  e.windows_key_code = windows_key(keyval); e.native_key_code = keycode;
  e.modifiers = modifiers(state); e.is_system_key = (state & GDK_ALT_MASK) != 0;
  e.character = gdk_keyval_to_unicode(keyval); e.unmodified_character = gdk_keyval_to_unicode(gdk_keyval_to_lower(keyval));
  browser_->GetHost()->SendKeyEvent(e);
}
bool Browser::canvas_input(const std::string& operation,double x,double y,double dx,double dy,const std::string& shortcut) {
  if (!browser_ || closing || loading) return false;
  CefRect rect; GetViewRect(browser_,rect);
  const auto inside=[&](double x,double y) {return std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<rect.width&&y<rect.height;};
  if (!inside(x,y) || !std::isfinite(dx) || !std::isfinite(dy)) return false;
  auto host=browser_->GetHost();
  const auto keycode=[&](guint value) {
    GdkKeymapKey* entries=nullptr;int count=0;
    const bool mapped=gdk_display_map_keyval(gtk_widget_get_display(area_),value,&entries,&count);
    const guint code=mapped&&count>0?entries[0].keycode:0;g_free(entries);return code;
  };
  CefMouseEvent event; event.x=std::lround(x);event.y=std::lround(y);event.modifiers=0;
  if(operation=="move") host->SendMouseMoveEvent(event,false);
  else if(operation=="click") {
    host->SendMouseMoveEvent(event,false);host->SendMouseClickEvent(event,MBT_LEFT,false,1);host->SendMouseClickEvent(event,MBT_LEFT,true,1);
  } else if (operation=="pan_drag"||operation=="frame_drag"||operation=="design_drag") {
    if (std::abs(dx)>2000 || std::abs(dy)>2000 || !inside(x+dx,y+dy)) return false;
    const bool left=operation!="pan_drag";
    const auto button=left?MBT_LEFT:MBT_MIDDLE;
    const bool alt=operation=="design_drag"&&(shortcut=="Alt"||shortcut=="Alt+Shift"),shift=operation=="design_drag"&&(shortcut=="Shift"||shortcut=="Alt+Shift");
    if(operation=="design_drag"&&!shortcut.empty()&&!alt&&!shift) return false;
    const auto alt_code=keycode(GDK_KEY_Alt_L),shift_code=keycode(GDK_KEY_Shift_L);
    if((alt&&!alt_code)||(shift&&!shift_code)) return false;
    host->SetFocus(true);
    if(alt) send_key(GDK_KEY_Alt_L,alt_code,GDK_ALT_MASK,false);
    if(shift) send_key(GDK_KEY_Shift_L,shift_code,static_cast<GdkModifierType>((alt?GDK_ALT_MASK:0)|GDK_SHIFT_MASK),false);
    const uint32_t held=(alt?EVENTFLAG_ALT_DOWN:0)|(shift?EVENTFLAG_SHIFT_DOWN:0);
    event.modifiers=held;
    host->SendMouseMoveEvent(event,false);host->SendMouseClickEvent(event,button,false,1);
    event.modifiers=held|(left?EVENTFLAG_LEFT_MOUSE_BUTTON:EVENTFLAG_MIDDLE_MOUSE_BUTTON);
    for(int i=1;i<=16;i++) {event.x=std::lround(x+dx*i/16);event.y=std::lround(y+dy*i/16);host->SendMouseMoveEvent(event,false);}
    event.modifiers=held;host->SendMouseClickEvent(event,button,true,1);
    if(shift) send_key(GDK_KEY_Shift_L,shift_code,static_cast<GdkModifierType>(alt?GDK_ALT_MASK:0),true);
    if(alt) send_key(GDK_KEY_Alt_L,alt_code,static_cast<GdkModifierType>(0),true);
    event.modifiers=0;host->SendMouseMoveEvent(event,false);
  } else if (operation=="zoom_wheel") {
    if (std::abs(dy)>600 || dy==0 || dx!=0) return false;
    event.modifiers=EVENTFLAG_CONTROL_DOWN;
    host->SendMouseMoveEvent(event,false);host->SendMouseWheelEvent(event,0,std::lround(dy));
    event.modifiers=0;host->SendMouseMoveEvent(event,false);
  } else if (operation=="shortcut") {
    guint key=0;GdkModifierType state=static_cast<GdkModifierType>(0);
    if(shortcut=="fit_page") {key=GDK_KEY_1;state=GDK_SHIFT_MASK;}
    else if(shortcut=="fit_selection") {key=GDK_KEY_2;state=GDK_SHIFT_MASK;}
    else if(shortcut=="zoom_100") {key=GDK_KEY_0;state=GDK_SHIFT_MASK;}
    else if(shortcut=="zoom_in") {key=GDK_KEY_plus;state=GDK_SHIFT_MASK;}
    else if(shortcut=="zoom_out") key=GDK_KEY_minus;
    else if(shortcut=="duplicate") {key=GDK_KEY_d;state=GDK_CONTROL_MASK;}
    else if(shortcut=="frame_tool") key=GDK_KEY_f;
    else if(shortcut=="pen_tool") key=GDK_KEY_p;
    else if(shortcut=="select_tool") key=GDK_KEY_v;
    else if(shortcut=="escape") key=GDK_KEY_Escape;
    else if(shortcut=="select_parent") {key=GDK_KEY_Return;state=GDK_SHIFT_MASK;}
    else if(shortcut.starts_with("key:")) {
      const auto value=shortcut.substr(4);
      if(value=="Escape") key=GDK_KEY_Escape;
      else if(value=="Enter"||value=="Shift+Enter") {key=GDK_KEY_Return;if(value=="Shift+Enter")state=GDK_SHIFT_MASK;}
      else if(value=="Delete") key=GDK_KEY_Delete;
      else if(value=="Backspace") key=GDK_KEY_BackSpace;
      else if(value=="Shift+1"||value=="Shift+2"||value=="Shift+0") {key=GDK_KEY_0+(value.back()-'0');state=GDK_SHIFT_MASK;}
      else if(value=="Shift++") {key=GDK_KEY_plus;state=GDK_SHIFT_MASK;}
      else if(value=="-") key=GDK_KEY_minus;
      else if(value=="Shift+P") {key=GDK_KEY_p;state=GDK_SHIFT_MASK;}
      else if(value.size()==1&&std::string("PVFROLT").find(value[0])!=std::string::npos) key=GDK_KEY_a+(value[0]-'A');
      else return false;
    }
    else return false;
    const auto modifier=state==GDK_CONTROL_MASK?GDK_KEY_Control_L:GDK_KEY_Shift_L;
    const auto code=keycode(key),modifier_code=keycode(modifier);
    if(!code || (state&&!modifier_code)) return false;
    host->SetFocus(true);
    if(state) send_key(modifier,modifier_code,state,false);
    send_key(key,code,state,false);send_key(key,code,state,true);
    if(state) send_key(modifier,modifier_code,static_cast<GdkModifierType>(0),true);
  } else return false;
  // Every replayed key and mouse button is released in this call, including
  // when the next step is cancelled. This never moves the desktop pointer.
  return true;
}
void Browser::OnTitleChange(CefRefPtr<CefBrowser> source, const CefString& title) {
  if (!owns(source)) return;
  name = owner() && owner()->app.auth_test ? title.ToString() : is_home ? "Home" : Policy::title(url, title);
  changed();
}
void Browser::OnAddressChange(CefRefPtr<CefBrowser> source, CefRefPtr<CefFrame> frame, const CefString& address) {
  if (!owns(source)) return;
  if (!frame->IsMain()) return;
  // Chromium reports its initial blank document before navigating to the requested URL.
  if (address == "about:blank" && url != "about:blank") {
    if (is_auth_popup && auth_has_document_) schedule_auth_return("about:blank");
    return;
  }
  const std::string target = address;
  if (is_auth_popup && !target.empty() && target != "about:blank") auth_has_document_ = true;
  if (!is_auth_popup && !Policy::is_new_tab(url) && Policy::is_file(target) && Policy::file_key(target) != Policy::file_key(url) && Policy::is_figma(url) && owner()) {
    if (owner()->zane_account) owner()->open(target); else owner()->app.open(target);
    if (browser_ && browser_->CanGoBack()) browser_->GoBack();
    else if (browser_) browser_->GetMainFrame()->LoadURL(url);
    return;
  }
  url = address; name = is_home ? "Home" : Policy::title(url, name); changed();
  if (owner() && owner()->zane_account && Policy::is_sign_in(url)) gtk_window_present(owner()->window);
  // History API navigation does not produce OnLoadEnd. Attach to files
  // opened from Figma's in-page file browser as well as full navigations.
  if (Policy::is_file(url) && !(owner() && owner()->zane_account)) { if (!zane_) zane_ = std::make_unique<Zane>(*this); zane_->ensure(frame); }
  else if (zane_) zane_->stop();
  set_permissions();
  refresh_theme();
}
void Browser::OnLoadingStateChange(CefRefPtr<CefBrowser> source, bool is_loading, bool, bool) { if (!owns(source)) return; loading = is_loading; changed(); }
void Browser::OnLoadEnd(CefRefPtr<CefBrowser> source, CefRefPtr<CefFrame> frame, int status) {
  if (!owns(source)) return;
  if (!frame->IsMain()) return;
  const std::string completed = frame->GetURL();
  refresh_theme();
  if (owner() && owner()->zane_account) {
    if(owner()->app.render_test&&g_getenv("FIGMA_GTK_TEST_ZANE_USER_MODE")&&completed.starts_with("data:text/html;base64,")) frame->ExecuteJavaScript(R"JS(
      window.fixtureCurrentPage='2:1';window.fixtureState.mirror.appModel.currentPage='2:1';
      window.fixtureConnectionFailures=0;
      window.fixturePageReady=()=>{
        if(window.fixtureConnectionFailures++===0) {
          window.fixtureCurrentPage='2:1';window.fixtureState.mirror.appModel.currentPage='2:1';
          throw Error('Offline transient page-load failure');
        }
      };
    )JS","figma-gtk://diagnostic",1);
    Zane::refresh_account(*this);
  }
  else if (Policy::is_file(completed) || (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_ZANE"))) {
    if (!zane_) zane_ = std::make_unique<Zane>(*this);
    zane_->install(frame);
  } else if (zane_) zane_->stop();
  // Preserve window.opener through the entire OAuth callback. A callback can
  // deliberately leave a blank document instead of calling window.close().
  const bool auth_return = is_auth_popup && status < 400 &&
    ((Policy::is_figma(completed) && !Policy::is_sign_in(completed)) ||
     (completed == "about:blank" && auth_has_document_));
  if (is_auth_popup && completed != "about:blank") auth_has_document_ = true;
  if (auth_return) schedule_auth_return(completed);
  if (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_CURSOR")) {
    frame->ExecuteJavaScript(R"JS(document.body.style.cursor = 'url("data:image/svg+xml;base64,' + btoa('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><path d="M4 4L4 27L11 21L17 29L22 26L16 18L26 17Z" fill="#3584e4" stroke="white" stroke-width="2"/></svg>') + '") 4 4, auto';)JS", "figma-gtk://diagnostic", 1);
    g_timeout_add_full(G_PRIORITY_DEFAULT, 300, +[](gpointer p) -> gboolean {
      auto browser = *static_cast<CefRefPtr<Browser>*>(p);
      if (browser->cef_browser()) { CefMouseEvent event; event.x = 20; event.y = 20; browser->cef_browser()->GetHost()->SendMouseMoveEvent(event, false); }
      return G_SOURCE_REMOVE;
    }, new CefRefPtr<Browser>(this), +[](gpointer p) { delete static_cast<CefRefPtr<Browser>*>(p); });
  }
  if (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_CLIPBOARD") && url.rfind("data:text/html;base64,", 0) == 0) {
    frame->ExecuteJavaScript("const input = document.querySelector('input'); input.focus(); input.addEventListener('input', () => { if (input.value === 'Native clipboard ✓') document.title = 'WebGL + Clipboard OK'; });", "figma-gtk://diagnostic", 1);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(widget()), "Native clipboard ✓");
    g_timeout_add(100, +[](gpointer p) -> gboolean {
      auto* browser = static_cast<CefRefPtr<Browser>*>(p); Clipboard::paste(**browser); delete browser; return G_SOURCE_REMOVE;
    }, new CefRefPtr<Browser>(this));
  }
  if (status >= 400) show_error("Could Not Open Page", "Figma returned an error. Try again, or open another file.");
  if (owner() && !owner()->zane_account && Policy::is_file(url)) { owner()->app.state.remember({url, name}); owner()->app.state.save(); owner()->refresh_home(); }
}
void Browser::schedule_auth_return(const std::string& completed) {
  if (auth_return_pending_ || closing) return;
  auth_return_pending_ = true;
  struct Return { CefRefPtr<Browser> browser; std::string url; };
  g_timeout_add(1000, +[](gpointer p) -> gboolean {
    auto* pending = static_cast<Return*>(p); auto browser = pending->browser;
    browser->auth_return_pending_ = false;
    if (browser->browser_ && !browser->closing && !browser->loading &&
        browser->browser_->GetMainFrame()->GetURL().ToString() == pending->url && browser->owner())
      browser->owner()->finish_auth(browser.get());
    delete pending; return G_SOURCE_REMOVE;
  }, new Return{this, completed});
}
void Browser::OnLoadError(CefRefPtr<CefBrowser> source, CefRefPtr<CefFrame> frame, ErrorCode error, const CefString& text, const CefString&) {
  if (!owns(source)) return;
  if (frame->IsMain() && error != ERR_ABORTED) show_error("Could Not Connect to Figma", "Check your internet connection and try again.");
}
void Browser::OnFullscreenModeChange(CefRefPtr<CefBrowser> source, bool fullscreen) {
  if (!owns(source)) return;
  if (!owner()) return;
  if (fullscreen) gtk_window_fullscreen(owner()->window); else gtk_window_unfullscreen(owner()->window);
}
bool Browser::OnTooltip(CefRefPtr<CefBrowser>, CefString& text) { gtk_widget_set_tooltip_text(area_, text.ToString().c_str()); return true; }
bool Browser::OnProcessMessageReceived(CefRefPtr<CefBrowser> source_browser,CefRefPtr<CefFrame> frame,CefProcessId source,CefRefPtr<CefProcessMessage> message) {
  if (!owns(source_browser)) return false;
  if (Zane::receive_account(*this,frame,source,message)) return true;
  if (zane_ && zane_->receive(frame,source,message)) return true;
  return Clipboard::receive(*this,frame,source,message);
}
bool Browser::OnConsoleMessage(CefRefPtr<CefBrowser> source_browser, cef_log_severity_t, const CefString& message, const CefString& source, int) {
  if (!owns(source_browser)) return true;
  if (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_EDITOR_THEME") && message.ToString().starts_with("select-editor-theme:")) {
    const bool passed = message.ToString().find(":FAIL") == std::string::npos;
    g_print("Editor theme diagnostic: %s; %s\n", passed ? "PASS" : "FAIL", message.ToString().c_str());
    if (!passed) owner()->app.diagnostic_failed = true;
    const int checks = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget()), "editor-theme-checks"));
    g_object_set_data(G_OBJECT(widget()), "editor-theme-checks", GINT_TO_POINTER(checks+1));
    return true;
  }
  if(owner()&&owner()->app.render_test&&g_getenv("FIGMA_GTK_TEST_ZANE_ACTIONS")&&source.ToString()=="figma-gtk://diagnostic"&&message.ToString().starts_with("select-zane-canvas-actions:")) {
    const bool passed=message.ToString().ends_with(":PASS");
    g_object_set_data(G_OBJECT(widget()),"zane-canvas-actions-ok",GINT_TO_POINTER(passed));
    g_print("Zane trusted canvas input and duplicate pen drawing diagnostic: %s\n",passed?"PASS":"FAIL");
    if(!passed) g_print("Canvas input evidence: %s\n",message.ToString().c_str());
    return true;
  }
  if (owner() && owner()->app.render_test && source.ToString()=="figma-gtk://diagnostic" && message.ToString().starts_with("select-native-cursor-user-follow:")) {
    const bool passed=message.ToString().ends_with(":PASS");
    g_object_set_data(G_OBJECT(widget()),"native-cursor-user-follow-ok",GINT_TO_POINTER(passed));
    g_print("Zane follows user through canvas pan diagnostic: %s\n",passed ? "PASS" : "FAIL"); return true;
  }
  if (owner() && owner()->app.auth_test && g_getenv("FIGMA_GTK_TEST_ZANE_ACCOUNT") && source.ToString()=="figma-gtk://diagnostic" && message.ToString().starts_with("select-zane-account-isolation:")) {
    const bool passed=message.ToString().ends_with(":PASS");
    g_object_set_data(G_OBJECT(widget()),"zane-account-isolation-ok",GINT_TO_POINTER(passed));
    g_print("Separate account cookie isolation diagnostic: %s\n",passed ? "PASS" : "FAIL"); return true;
  }
  if (owner() && owner()->app.render_test && source.ToString()=="figma-gtk://diagnostic" &&
      (message.ToString().starts_with("select-native-cursor-follow:") || message.ToString().starts_with("select-zane-followup:"))) {
    const bool follow=message.ToString().starts_with("select-native-cursor-follow:");
    const bool passed=message.ToString().ends_with(":PASS");
    g_object_set_data(G_OBJECT(widget()),follow ? "native-cursor-follow-ok" : "zane-followup-ok",GINT_TO_POINTER(passed));
    g_print("%s diagnostic: %s\n",follow ? "Offscreen cursor and follow" : "Busy cursor chat and leave",passed ? "PASS" : "FAIL"); return true;
  }
  if (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_ZANE_CURSOR") && source.ToString() == "figma-gtk://diagnostic" && message.ToString().starts_with("select-native-cursor-pan:")) {
    const bool passed = message.ToString() == "select-native-cursor-pan:PASS";
    g_object_set_data(G_OBJECT(widget()),"native-cursor-pan-ok",GINT_TO_POINTER(passed));
    g_print("Native cursor stays anchored during canvas pan diagnostic: %s\n",passed ? "PASS" : "FAIL"); return true;
  }
  if (owner() && owner()->app.render_test && g_getenv("FIGMA_GTK_TEST_COPY") && source.ToString() == "figma-gtk://clipboard") g_print("Clipboard script diagnostic: %s\n",message.ToString().c_str());
  if (owner() && owner()->app.auth_test) g_print("Local fixture: %s\n", message.ToString().c_str());
  return false;
}
bool Browser::OnCursorChange(CefRefPtr<CefBrowser> source, CefCursorHandle, cef_cursor_type_t type, const CefCursorInfo& info) {
  if (!owns(source)) return true;
  if (type == CT_CUSTOM) {
    if (GdkCursor* cursor = make_custom_cursor(info)) {
      gtk_widget_set_cursor(area_, cursor); g_object_unref(cursor);
      custom_cursor_seen = true; return true;
    }
  }
  const char* cursor = "default";
  switch (type) {
    case CT_HAND: cursor = "pointer"; break; case CT_IBEAM: cursor = "text"; break;
    case CT_CROSS: cursor = "crosshair"; break; case CT_MOVE: cursor = "move"; break;
    case CT_NORTHSOUTHRESIZE: cursor = "ns-resize"; break;
    case CT_EASTWESTRESIZE: cursor = "ew-resize"; break;
    case CT_NORTHEASTSOUTHWESTRESIZE: cursor = "nesw-resize"; break;
    case CT_NORTHWESTSOUTHEASTRESIZE: cursor = "nwse-resize"; break;
    case CT_GRAB: cursor = "grab"; break; case CT_GRABBING: cursor = "grabbing"; break;
    case CT_NONE: cursor = "none"; break; default: break;
  }
  gtk_widget_set_cursor_from_name(area_, cursor); return true;
}
bool Browser::OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString& origin,
    uint32_t permissions, CefRefPtr<CefPermissionPromptCallback> callback) {
  const auto* win = owner();
  const bool allowed = win &&
    ((permissions == CEF_PERMISSION_TYPE_LOOPBACK_NETWORK && Policy::allow_font_loopback(origin, url, win->app.state.local_fonts)) ||
     (permissions == CEF_PERMISSION_TYPE_NOTIFICATIONS && win->app.state.notifications && Policy::is_figma(origin) && Policy::is_figma(url)));
  callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
  return true;
}
bool Browser::route(const std::string& target, bool user_gesture, bool new_tab) {
  if (!owner()) return false;
  if (Policy::is_sign_in(target) || Policy::is_auth(target)) owner()->app.authentication_window=owner();
  if (owner()->app.auth_test && target.rfind("http://127.0.0.1:", 0) == 0) return false;
  if (owner()->app.render_test && target.rfind("data:text/html;base64,", 0) == 0) return false;
  if (Policy::is_sign_in(target) && !is_home) {
    owner()->native_home();
    if (auto home = owner()->current()) home->navigate(target);
    return true;
  }
  const std::string normalized = Policy::normalize(target);
  const std::string auth = Policy::authentication_return(target);
  if (!auth.empty()) { owner()->app.open(target); return true; }
  if (is_auth_popup && Policy::is_figma(target)) return false;
  if (!normalized.empty()) {
    if (normalized != target) { if (owner()->zane_account) owner()->open(normalized); else owner()->app.open(normalized); return true; }
    if (new_tab && Policy::is_new_tab(url) && Policy::is_file(normalized)) {
      navigate(normalized); return true;
    }
    if (new_tab || (!Policy::is_new_tab(url) && Policy::is_file(normalized) && Policy::file_key(normalized) != Policy::file_key(url))) {
      owner()->open(normalized); return true;
    }
    return false;
  }
  if (Policy::is_auth(target)) return false;
  if (target == "about:blank" || target.rfind("blob:", 0) == 0) return false;
  if (Policy::is_http(target)) { if (user_gesture) owner()->app.external(target); return true; }
  return true;
}
bool Browser::OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool user_gesture, bool) {
  return frame->IsMain() && route(request->GetURL(), user_gesture, false);
}
bool Browser::OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& target, cef_window_open_disposition_t, bool user_gesture) {
  return route(target, user_gesture, true);
}
bool Browser::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& target,
    const CefString&, cef_window_open_disposition_t, bool user_gesture, const CefPopupFeatures&, CefWindowInfo& info,
    CefRefPtr<CefClient>& client, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) {
  if (!owner()) return true;
  const std::string address = target;
  if (owner()->app.auth_test) g_print("Local popup requested\n");
  if (Policy::is_sign_in(address) || address.empty() || address == "about:blank" ||
      (Policy::is_figma(address) && !Policy::is_file(address) && !Policy::is_new_tab(address)) ||
      (owner()->app.auth_test && address.rfind("http://127.0.0.1:", 0) == 0)) {
    owner()->app.authentication_window=owner();
    CefRefPtr<Browser> popup = owner()->popup(address); popup->loaded = true; popup->is_auth_popup = true;
    client = popup; info.SetAsWindowless(0); info.shared_texture_enabled = !software_;
    return false;
  }
  route(address, user_gesture, true); return true;
}
bool Browser::OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>, const CefString& suggested,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!owner()) return true;
  GtkFileDialog* dialog = gtk_file_dialog_new(); gtk_file_dialog_set_title(dialog, "Save Export");
  char* basename = g_path_get_basename(suggested.ToString().c_str());
  gtk_file_dialog_set_initial_name(dialog, basename); g_free(basename);
  GFile* folder = g_file_new_for_path(owner()->app.state.download_folder.c_str());
  gtk_file_dialog_set_initial_folder(dialog, folder); g_object_unref(folder);
  auto* cb = new CefRefPtr<CefBeforeDownloadCallback>(callback);
  gtk_file_dialog_save(dialog, owner()->window, nullptr, +[](GObject* object, GAsyncResult* result, gpointer data) {
    auto* callback = static_cast<CefRefPtr<CefBeforeDownloadCallback>*>(data);
    GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(object), result, nullptr);
    if (file) { char* path = g_file_get_path(file); if (path) (*callback)->Continue(path, false); g_free(path); g_object_unref(file); }
    delete callback; g_object_unref(object);
  }, cb);
  return true;
}
void Browser::OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item, CefRefPtr<CefDownloadItemCallback>) {
  if (!owner()) return;
  if (item->IsComplete()) owner()->app.export_completed(item->GetFullPath(), url, name);
  else if (item->IsCanceled()) owner()->toast("Download cancelled");
}
void Browser::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) {
  model->Remove(MENU_ID_PRINT); model->Remove(MENU_ID_VIEW_SOURCE);
}
bool Browser::RunContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model, CefRefPtr<CefRunContextMenuCallback> callback) {
  GtkWidget* popover = gtk_popover_new(); GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(box, "menu"); gtk_popover_set_child(GTK_POPOVER(popover), box);
  gtk_widget_set_parent(popover, area_);
  auto* cb = new CefRefPtr<CefRunContextMenuCallback>(callback);
  g_object_set_data_full(G_OBJECT(popover), "cef-callback", cb, +[](gpointer p) { delete static_cast<CefRefPtr<CefRunContextMenuCallback>*>(p); });
  for (size_t i = 0; i < model->GetCount(); ++i) {
    if (model->GetTypeAt(i) == MENUITEMTYPE_SEPARATOR) { gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)); continue; }
    const std::string label = model->GetLabelAt(i);
    GtkWidget* button = gtk_button_new_with_label(label.c_str()); gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_sensitive(button, model->IsEnabledAt(i)); gtk_box_append(GTK_BOX(box), button);
    g_object_set_data(G_OBJECT(button), "command", GINT_TO_POINTER(model->GetCommandIdAt(i)));
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, GtkPopover* pop) {
      auto* cb = static_cast<CefRefPtr<CefRunContextMenuCallback>*>(g_object_get_data(G_OBJECT(pop), "cef-callback"));
      (*cb)->Continue(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "command")), EVENTFLAG_NONE);
      *cb = nullptr; gtk_popover_popdown(pop);
    }), popover);
  }
  g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover* pop, gpointer) {
    auto* cb = static_cast<CefRefPtr<CefRunContextMenuCallback>*>(g_object_get_data(G_OBJECT(pop), "cef-callback"));
    if (*cb) (*cb)->Cancel();
    gtk_widget_unparent(GTK_WIDGET(pop));
  }), nullptr);
  GdkRectangle rect = {params->GetXCoord(), params->GetYCoord(), 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect); gtk_popover_popup(GTK_POPOVER(popover));
  return true;
}
namespace {
struct DialogData {
  CefRefPtr<CefJSDialogCallback> callback;
  CefRefPtr<Browser> browser;
  GtkWidget* entry = nullptr;
  bool unload = false;
};
void dialog_response(AdwAlertDialog*, const char* response, DialogData* data) {
  const bool accept = std::strcmp(response, "accept") == 0;
  const char* value = data->entry ? gtk_editable_get_text(GTK_EDITABLE(data->entry)) : "";
  data->callback->Continue(accept, value);
  if (!accept && data->unload) {
    data->browser->closing = false;
    if (data->browser->on_close_cancelled) data->browser->on_close_cancelled();
    if (data->browser->owner()) data->browser->owner()->closing = false;
  }
}
}
bool Browser::OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType type, const CefString& message,
    const CefString& prompt, CefRefPtr<CefJSDialogCallback> callback, bool& suppress) {
  if (!owner()) { callback->Continue(false, ""); return true; }
  AdwDialog* dialog = adw_alert_dialog_new("Figma", message.ToString().c_str());
  auto* data = new DialogData{callback, this};
  if (type != JSDIALOGTYPE_ALERT) adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "accept", "_OK");
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "accept");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), type == JSDIALOGTYPE_ALERT ? "accept" : "cancel");
  if (type == JSDIALOGTYPE_PROMPT) {
    data->entry = gtk_entry_new(); gtk_editable_set_text(GTK_EDITABLE(data->entry), prompt.ToString().c_str());
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), data->entry);
  }
  g_signal_connect_data(dialog, "response", G_CALLBACK(dialog_response), data,
      +[](gpointer p, GClosure*) { delete static_cast<DialogData*>(p); }, G_CONNECT_DEFAULT);
  adw_dialog_present(dialog, GTK_WIDGET(owner()->window)); return true;
}
bool Browser::OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool,
    CefRefPtr<CefJSDialogCallback> callback) {
  if (!owner()) { callback->Continue(true, ""); return true; }
  AdwDialog* dialog = adw_alert_dialog_new("Leave This File?", "Figma is still working on this file. Changes may not have finished syncing.");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "_Keep Open");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "accept", "_Leave");
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "accept", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  auto* data = new DialogData{callback, this, nullptr, true};
  g_signal_connect_data(dialog, "response", G_CALLBACK(dialog_response), data,
      +[](gpointer p, GClosure*) { delete static_cast<DialogData*>(p); }, G_CONNECT_DEFAULT);
  adw_dialog_present(dialog, GTK_WIDGET(owner()->window)); return true;
}
