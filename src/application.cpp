#include "application.hpp"
#include "include/cef_request_context_handler.h"
#include "fonts.hpp"
#include "policy.hpp"
#include "clipboard.hpp"
#include "build-info.hpp"
#include "keepassxc.hpp"
#include "workspace-tabs.hpp"
#include "zane.hpp"
#include "include/cef_version.h"
#include "include/cef_cookie.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <sodium.h>
#include <json-glib/json-glib.h>

namespace {
constexpr const char* app_id = "io.github.doromiert.FigmaGtk";
GtkWidget* icon_button(const char* icon, const char* tooltip, const char* action) {
  GtkWidget* button = gtk_button_new_from_icon_name(icon);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, tooltip, -1);
  if (action) gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);
  return button;
}
void section(GMenu* root, std::initializer_list<std::pair<const char*, const char*>> items) {
  GMenu* menu = g_menu_new();
  for (const auto& item : items) g_menu_append(menu, item.first, item.second);
  g_menu_append_section(root, nullptr, G_MENU_MODEL(menu)); g_object_unref(menu);
}
void action(GActionMap* map, const char* name, GCallback cb, gpointer user) {
  GSimpleAction* a = g_simple_action_new(name, nullptr);
  g_signal_connect(a, "activate", cb, user); g_action_map_add_action(map, G_ACTION(a)); g_object_unref(a);
}
void set_enabled(GtkWindow* win, const char* name, bool enabled) {
  auto* a = g_action_map_lookup_action(G_ACTION_MAP(win), name);
  if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), enabled);
}
void launch_file(GtkWindow* parent, const std::string& path, bool folder) {
  GFile* file = g_file_new_for_path(path.c_str());
  GtkFileLauncher* launcher = gtk_file_launcher_new(file); g_object_unref(file);
  if (folder) gtk_file_launcher_open_containing_folder(launcher, parent, nullptr, nullptr, nullptr);
  else gtk_file_launcher_launch(launcher, parent, nullptr, nullptr, nullptr);
  g_object_unref(launcher);
}
}

Application::Application() {
  gtk_app = adw_application_new(app_id, G_APPLICATION_HANDLES_OPEN);
  auto* build = g_simple_action_new_stateful("build-id",nullptr,g_variant_new_string(APP_BUILD_ID));
  g_action_map_add_action(G_ACTION_MAP(gtk_app),G_ACTION(build)); g_object_unref(build);
  g_set_application_name("Select for Figma");
  gtk_window_set_default_icon_name(app_id);
  g_signal_connect_after(gtk_app, "startup", G_CALLBACK(+[](GApplication*, gpointer) {
    gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), "/io/github/doromiert/FigmaGtk/icons");
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
      ".tab-switcher { background: var(--popover-bg-color); color: var(--popover-fg-color); border-radius: 16px; padding: 12px; box-shadow: 0 8px 24px rgb(0 0 0 / 24%); }"
      ".tab-switcher flowbox { background: transparent; }"
      ".tab-switcher flowboxchild { border-radius: 12px; padding: 6px; }"
      ".tab-switcher .thumbnail { border-radius: 7px; background: var(--view-bg-color); box-shadow: 0 0 0 1px rgb(0 0 0 / 12%); }"
      ".workspace-tabs .document-tab { min-width: 180px; min-height: 34px; border-radius: 9px; font-weight: normal; }"
      ".zane-bubble { background-color: var(--popover-bg-color); color: var(--popover-fg-color); padding: 12px; border-radius: 12px; box-shadow: 0 2px 8px rgb(0 0 0 / 15%); }"
      ".workspace-tabs .document-tab.hovering { background-color: color-mix(in srgb, currentColor 7%, transparent); }"
      ".workspace-tabs .document-tab.selected { background-color: color-mix(in srgb, currentColor 10%, transparent); }"
      ".workspace-tabs .document-tab.selected.hovering { background-color: color-mix(in srgb, currentColor 13%, transparent); }"
      ".workspace-tabs .tab-label { min-width: 64px; min-height: 34px; padding: 0 6px; background: none; font-weight: normal; }"
      ".workspace-tabs .tab-close { min-width: 34px; min-height: 34px; padding: 0; }"
      ".workspace-tabs .group-collapse { min-width: 34px; min-height: 34px; padding: 0; }"
      ".workspace-tabs .tab-close, .workspace-tabs .tab-close:hover, .workspace-tabs .tab-close:active { background: none; box-shadow: none; border-radius: 99px; }"
      ".workspace-tabs .tab-close image { min-width: 24px; min-height: 24px; border-radius: 99px; }"
      ".workspace-tabs .tab-close:hover image { background-color: color-mix(in srgb, currentColor 10%, transparent); }"
      ".workspace-tabs .tab-close:active image { background-color: color-mix(in srgb, currentColor 16%, transparent); }"
      ".workspace-tabs .drag-placeholder { opacity: 0; }"
      ".workspace-tabs .group-drop-candidate { background-color: color-mix(in srgb, currentColor 16%, transparent); }"
      ".select-tab-overview .group-drop-candidate .thumbnail { outline: 2px solid color-mix(in srgb, currentColor 45%, transparent); outline-offset: 2px; }"
      ".workspace-tabs:drop(active), .workspace-tabs *:drop(active) { box-shadow: none; outline: none; }"
      ".workspace-tabs .document-tab:not(.selected):not(.hovering) .tab-close { opacity: 0; }"
      ".workspace-tabs .tab-group { min-width: 180px; min-height: 28px; border-radius: 9px; padding: 3px; background-color: color-mix(in srgb, var(--select-group-color, var(--accent-bg-color)) 18%, transparent); }"
      ".workspace-tabs button.tab-group { padding: 3px 8px; min-height: 28px; }"
      ".workspace-tabs .tab-group.active-group { background-color: color-mix(in srgb, var(--select-group-color, var(--accent-bg-color)) 28%, transparent); }"
      ".workspace-tabs .tab-group.expanded { min-height: 34px; padding: 0; background: none; border-radius: 0; }"
      ".group-card { border-radius: 16px; padding: 6px; background: none; font-weight: normal; }"
      ".group-card .group-preview-frame { border-radius: 12px; background-color: color-mix(in srgb, var(--select-group-color, var(--accent-bg-color)) 18%, transparent); }"
      ".group-card .group-preview { border-radius: 6px; background-color: color-mix(in srgb, currentColor 8%, transparent); }"
      ".group-card:hover .group-preview-frame { background-color: color-mix(in srgb, var(--select-group-color, var(--accent-bg-color)) 24%, transparent); }"
      ".group-drawer { border-radius: 12px; padding: 16px; background-color: color-mix(in srgb, var(--select-group-color, var(--accent-bg-color)) 12%, var(--popover-bg-color)); }"
      ".select-tab-overview { background-color: var(--overview-bg-color); color: var(--overview-fg-color); }"
      ".select-tab-overview .new-tab-button { margin: 18px; }"
      ".select-tab-overview .overview-home { min-height: 40px; padding: 0 16px; border-radius: 12px; font-weight: normal; background-color: var(--card-bg-color); transition: opacity 250ms ease, background-color 200ms ease; }"
      ".select-tab-overview .overview-home:hover { background-color: color-mix(in srgb, currentColor 7%, var(--card-bg-color)); }"
      ".select-tab-overview .overview-home.active-home { background-color: color-mix(in srgb, var(--accent-bg-color) 14%, var(--card-bg-color)); }"
      ".overview-tab { border-radius: 16px; font-weight: normal; }"
      ".overview-tab .tab-label { padding: 6px; font-weight: normal; background: none; box-shadow: none; border-radius: 16px; }"
      ".overview-tab .thumbnail { border-radius: 12px; background-color: var(--window-bg-color); box-shadow: 0 1px 3px 1px rgb(0 0 0 / 7%), 0 2px 6px 2px rgb(0 0 0 / 3%); outline: 1px solid rgb(0 0 0 / 7%); outline-offset: -1px; }"
      ".overview-tab .tab-label:focus-visible .thumbnail { outline: 2px solid var(--accent-color); outline-offset: 2px; }"
      ".overview-tab .tab-close { margin: 12px; min-width: 24px; min-height: 24px; padding: 0; border-radius: 99px; background-color: color-mix(in srgb, var(--window-bg-color) 75%, transparent); }"
      ".overview-tab:not(.hovering):not(:focus-within) .tab-close { opacity: 0; }"
      ".overview-tab, .group-card { transition: opacity 250ms ease; }"
      ".select-tab-overview .dimmed { opacity: .45; }"
      ".select-tab-overview .drag-placeholder { opacity: 0; transition: none; }"
      ".select-tab-overview:drop(active), .select-tab-overview *:drop(active) { box-shadow: none; outline: none; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
  }), nullptr);
  g_signal_connect(gtk_app, "activate", G_CALLBACK(+[](GApplication*, Application* self) {
    if (auto* window = self->active_window()) gtk_window_present(window->window); else self->new_window();
  }), this);
  g_signal_connect(gtk_app, "open", G_CALLBACK(+[](GApplication*, GFile** files, int n, const char*, Application* self) {
    for (int i = 0; i < n; ++i) { char* uri = g_file_get_uri(files[i]); self->open(uri); g_free(uri); }
  }), this);
  g_signal_connect(gtk_app, "shutdown", G_CALLBACK(+[](GApplication*, Application* self) { self->persist(); self->state.save(); }), this);
  const struct { const char* action; const char* shortcut; } shortcuts[] = {
    {"win.new-tab", "<Primary>t"},
    {"win.open-link", "<Primary>o"}, {"win.home", "<Alt>Home"}, {"win.close-tab", "<Primary>w"},
    {"win.close-window", "<Primary><Shift>w"}, {"win.new-window", "<Primary>n"},
    {"win.reload", "<Primary>r"}, {"win.hard-reload", "<Primary><Shift>r"},
    {"win.back", "<Alt>Left"}, {"win.forward", "<Alt>Right"}, {"win.overview", "<Primary><Shift>o"},
    {"win.copy-link", "<Primary><Shift>c"}, {"win.preferences", "<Primary>comma"},
    {"win.shortcuts", "<Primary>question"}, {"win.fullscreen", "F11"},
    {"win.downloads", "<Primary>j"}, {"win.next-tab", "<Primary>Page_Down"}, {"win.previous-tab", "<Primary>Page_Up"},
  };
  for (const auto& s : shortcuts) { const char* accelerators[] = {s.shortcut, nullptr}; gtk_application_set_accels_for_action(GTK_APPLICATION(gtk_app), s.action, accelerators); }
}
Application::~Application() { if (save_source) g_source_remove(save_source); fonts.reset(); windows.clear(); g_object_unref(gtk_app); }
Window* Application::active_window() {
  GtkWindow* active = gtk_application_get_active_window(GTK_APPLICATION(gtk_app));
  if (active) {
    auto* window=static_cast<Window*>(g_object_get_data(G_OBJECT(active),"window"));
    if (window && !window->zane_account && !window->closing && !window->destroyed) return window;
  }
  for (auto& window : windows) if (!window->zane_account && !window->closing && !window->destroyed) return window.get();
  return nullptr;
}
Window* Application::new_window(bool present) {
  const bool restore = std::none_of(windows.begin(),windows.end(),[](const auto& window){return !window->zane_account;}) && state.restore_session;
  const auto saved = state.tabs;
  auto instance = std::make_unique<Window>(*this);
  Window* window = instance.get(); windows.push_back(std::move(instance));
  if (restore) for (const auto& tab : saved) window->open(tab.url, false, true)->group_id = tab.group_id;
  window->update();
  if (present) gtk_window_present(window->window);
  return window;
}
Window* Application::zane_account_window(bool present) {
  for (auto& window : windows) if (window->zane_account && !window->closing && !window->destroyed) {
    if (present) gtk_window_present(window->window);
    return window.get();
  }
  if (!zane_account_context) {
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) = state.cache_directory() + "/zane-account";
    settings.persist_session_cookies = true;
    zane_account_context = CefRequestContext::CreateContext(settings,nullptr);
  }
  if (!zane_account_context) return nullptr;
  auto instance = std::make_unique<Window>(*this,true);
  auto* window = instance.get(); windows.push_back(std::move(instance)); window->update();
  if (present) gtk_window_present(window->window);
  else if (auto browser=window->current()) browser->start();
  return window;
}
void Application::open(const std::string& url) {
  Window* win = active_window(); if (!win) win = new_window();
  std::string address = Policy::normalize(url);
  const auto authentication = Policy::authentication_return(url);
  if (!authentication.empty()) {
    if (authentication_window && !authentication_window->closing && !authentication_window->destroyed) win=authentication_window;
    authentication_window=nullptr;
    win->native_home();
    win->refresh_after_auth_ = false;
    win->home_browser_->navigate(authentication);
    const auto browsers = win->browsers_;
    for (auto& browser : browsers) if (browser->is_auth_popup) browser->close();
    gtk_stack_set_visible_child(GTK_STACK(win->home_), win->home_browser_->widget());
    return;
  }
  if (win->zane_account) {
    auto found=std::find_if(windows.begin(),windows.end(),[](const auto& window){return !window->zane_account && !window->closing && !window->destroyed;});
    win=found==windows.end() ? new_window() : found->get();
  }
  if (address.empty()) { win->toast("This is not a Figma link"); return; }
  if (Policy::is_sign_in(address)) {
    win->native_home(); if (auto home = win->current()) home->navigate(address); return;
  }
  // Reuse an existing document across all windows, preserving its canvas state.
  const std::string key = Policy::file_key(address);
  for (auto& window : windows) if (!window->destroyed && window->zane_account==win->zane_account) {
    for (auto& browser : window->browsers_) if (browser->url == address || (!key.empty() && Policy::file_key(browser->url) == key)) {
      if (browser->is_home) window->native_home();
      else {
        adw_tab_view_set_selected_page(window->tabs, adw_tab_view_get_page(window->tabs, browser->widget()));
        gtk_stack_set_visible_child_name(GTK_STACK(window->content_), "documents");
      }
      if (browser->url != address) browser->navigate(address);
      window->update();
      window->workspace->show_overview(false);
      gtk_window_present(window->window); return;
    }
  }
  win->open(address); gtk_window_present(win->window);
}
void Application::queue_save() {
  if (save_source) g_source_remove(save_source);
  save_source = g_timeout_add(750, +[](gpointer data) -> gboolean {
    auto* app = static_cast<Application*>(data); app->save_source = 0; app->persist(); app->prune_groups(); app->state.save(); return G_SOURCE_REMOVE;
  }, this);
}
bool Application::group_in_use(const std::string& id) const {
  for (const auto& window : windows) if (!window->destroyed && !window->closing)
    for (int i = 0; i < adw_tab_view_get_n_pages(window->tabs); ++i) {
      auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs,i))),"browser"));
      if (browser && !browser->closing && browser->group_id == id) return true;
    }
  return false;
}
void Application::prune_groups() {
  // Restore and transfer pages before removing metadata. The queued save runs
  // after those synchronous operations, including all windows in the check.
  if (std::none_of(windows.begin(),windows.end(),[](const auto& w) { return !w->destroyed && !w->closing; })) return;
  if (std::any_of(windows.begin(),windows.end(),[](const auto& w) { return !w->destroyed && adw_tab_view_get_is_transferring_page(w->tabs); })) return;
  auto& groups = state.groups;
  groups.erase(std::remove_if(groups.begin(),groups.end(),[&](const auto& g) { return !group_in_use(g.id); }),groups.end());
}
void Application::persist() {
  // Preserve the last workspace when its window closes. Normal tab closure updates it.
  if (windows.empty() || std::none_of(windows.begin(), windows.end(), [](const auto& w) { return !w->zane_account && !w->destroyed && !w->closing; })) return;
  std::vector<SavedTab> result;
  for (auto& window : windows) if (!window->zane_account && !window->destroyed && !window->closing) {
    for (int i = 0; i < adw_tab_view_get_n_pages(window->tabs); ++i) {
      AdwTabPage* page = adw_tab_view_get_nth_page(window->tabs, i);
      auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"));
      if (browser && Policy::is_file(browser->url)) result.push_back({browser->url, browser->name, browser->group_id});
    }
  }
  state.tabs = std::move(result);
}
void Application::apply_appearance() {
  AdwColorScheme scheme = state.appearance == "light" ? ADW_COLOR_SCHEME_FORCE_LIGHT :
      state.appearance == "dark" ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_DEFAULT;
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), scheme);
}
void Application::external(const std::string& url) {
  if (!Policy::is_http(url)) return;
  GtkUriLauncher* launcher = gtk_uri_launcher_new(url.c_str());
  auto* win = active_window();
  gtk_uri_launcher_launch(launcher, win ? win->window : nullptr, nullptr, nullptr, nullptr); g_object_unref(launcher);
}
void Application::export_completed(const std::string& path, const std::string& source, const std::string& title) {
  if (std::any_of(state.exports.begin(), state.exports.end(), [&](const auto& e) { return e.path == path && e.source == source; })) return;
  GDateTime* now = g_date_time_new_now_local(); char* date = g_date_time_format(now, "%x %X");
  state.exports.insert(state.exports.begin(), {path, source, title, date});
  if (state.exports.size() > 300) state.exports.resize(300);
  char* folder = g_path_get_dirname(path.c_str()); state.download_folder = folder; g_free(folder);
  g_free(date); g_date_time_unref(now); state.save();
  if (auto* win = active_window()) {
    AdwToast* toast = adw_toast_new("Export saved"); adw_toast_set_button_label(toast, "Show Downloads");
    adw_toast_set_action_name(toast, "win.downloads"); adw_toast_overlay_add_toast(win->toasts, toast);
  }
  if (state.notifications && !gtk_application_get_active_window(GTK_APPLICATION(gtk_app))) {
    GNotification* notification = g_notification_new("Export Saved");
    char* basename = g_path_get_basename(path.c_str()); g_notification_set_body(notification, basename); g_free(basename);
    g_application_send_notification(G_APPLICATION(gtk_app), "export", notification); g_object_unref(notification);
  }
}
int Application::run(int argc, char** argv) { return g_application_run(G_APPLICATION(gtk_app), argc, argv); }

Window::Window(Application& application, bool account) : app(application), zane_account(account) {
  window = GTK_WINDOW(adw_application_window_new(GTK_APPLICATION(app.gtk_app)));
  g_object_ref(window);
  capture_ = gtk_widget_paintable_new(GTK_WIDGET(window));
  g_object_set_data(G_OBJECT(window), "capture", capture_);
  g_object_set_data(G_OBJECT(window), "window", this);
  gtk_window_set_default_size(window, app.state.width, app.state.height);
  if (app.state.maximized) gtk_window_maximize(window);
  gtk_widget_set_size_request(GTK_WIDGET(window), 360, 294);
  gtk_window_set_title(window, "Select for Figma");
  tabs = ADW_TAB_VIEW(adw_tab_view_new());
  GIcon* default_icon = g_themed_icon_new("applications-graphics-symbolic");
  adw_tab_view_set_default_icon(tabs, default_icon); g_object_unref(default_icon);
  overview = ADW_TAB_OVERVIEW(adw_tab_overview_new()); adw_tab_overview_set_view(overview, tabs);
  adw_tab_overview_set_enable_new_tab(overview, false);
  toolbar_ = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  adw_header_bar_set_centering_policy(ADW_HEADER_BAR(header), ADW_CENTERING_POLICY_LOOSE);
  adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), false);
  adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), false);
  GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand(header_row, true);
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), header_row);
  GtkWidget* start_controls = gtk_window_controls_new(GTK_PACK_START);
  gtk_box_append(GTK_BOX(header_row), start_controls);
  g_signal_connect(start_controls, "notify::empty", G_CALLBACK(+[](GtkWindowControls* controls, GParamSpec*, gpointer) {
    gtk_widget_set_visible(GTK_WIDGET(controls), !gtk_window_controls_get_empty(controls));
  }), nullptr);
  gtk_widget_set_visible(start_controls, !gtk_window_controls_get_empty(GTK_WINDOW_CONTROLS(start_controls)));
  g_object_set_data(G_OBJECT(window), "main-header", header);
  home_button_ = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(home_button_), "go-home-symbolic");
  gtk_widget_set_tooltip_text(home_button_, "Home (Alt+Home)");
  gtk_accessible_update_property(GTK_ACCESSIBLE(home_button_), GTK_ACCESSIBLE_PROPERTY_LABEL, "Home", -1);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(home_button_), "win.home");
  gtk_box_append(GTK_BOX(header_row), home_button_);
  reload_ = icon_button("view-refresh-symbolic", "Reload (Ctrl+R)", "win.reload");
  gtk_box_append(GTK_BOX(header_row), reload_);
  title_ = ADW_WINDOW_TITLE(adw_window_title_new("Select for Figma", ""));
  workspace = std::make_unique<WorkspaceTabs>(*this);
  tab_bar_ = workspace->strip();
  gtk_widget_add_css_class(tab_bar_, "header-tabs");
  gtk_widget_set_hexpand(tab_bar_, true);
  header_content_ = gtk_stack_new();
  gtk_stack_set_hhomogeneous(GTK_STACK(header_content_), false);
  gtk_stack_add_named(GTK_STACK(header_content_), tab_bar_, "tabs");
  gtk_stack_add_named(GTK_STACK(header_content_), GTK_WIDGET(title_), "title");
  gtk_stack_set_visible_child_name(GTK_STACK(header_content_), "tabs");
  gtk_widget_set_hexpand(header_content_, true);
  gtk_box_append(GTK_BOX(header_row), header_content_);
  g_object_set_data(G_OBJECT(window), "header-tabs", tab_bar_);
  g_object_set_data(G_OBJECT(window), "header-reload", reload_);
  GtkWidget* tab_button = adw_tab_button_new(); adw_tab_button_set_view(ADW_TAB_BUTTON(tab_button), tabs);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(tab_button), "win.overview");
  gtk_widget_set_tooltip_text(tab_button, "Tab Overview (Ctrl+Shift+O)");
  gtk_accessible_update_property(GTK_ACCESSIBLE(tab_button), GTK_ACCESSIBLE_PROPERTY_LABEL, "Tab Overview", -1);
  GMenu* menu = g_menu_new();
  section(menu, {{"New Tab", "win.new-tab"}, {"New Tab Group…", "win.new-tab-group"}, {"Open Figma Link…", "win.open-link"}, {"New Window", "win.new-window"}});
  section(menu, {{"Open in Browser", "win.external"}, {"Downloads", "win.downloads"}, {"Fullscreen", "win.fullscreen"}});
  section(menu, {{"Fill from KeePassXC", "win.keepassxc-fill"}});
  section(menu, {{"Preferences", "win.preferences"}, {"Keyboard Shortcuts", "win.shortcuts"}, {"About Select for Figma", "win.about"}});
  GtkWidget* menu_button = gtk_menu_button_new(); gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu)); g_object_unref(menu);
  gtk_widget_set_tooltip_text(menu_button, "Main Menu");
  gtk_box_append(GTK_BOX(header_row), tab_button);
  gtk_box_append(GTK_BOX(header_row), menu_button);
  GtkWidget* end_controls = gtk_window_controls_new(GTK_PACK_END);
  gtk_box_append(GTK_BOX(header_row), end_controls);
  g_signal_connect(end_controls, "notify::empty", G_CALLBACK(+[](GtkWindowControls* controls, GParamSpec*, gpointer) {
    gtk_widget_set_visible(GTK_WIDGET(controls), !gtk_window_controls_get_empty(controls));
  }), nullptr);
  gtk_widget_set_visible(end_controls, !gtk_window_controls_get_empty(GTK_WINDOW_CONTROLS(end_controls)));
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_), header);
  content_ = gtk_stack_new();
  gtk_stack_add_named(GTK_STACK(content_), GTK_WIDGET(tabs), "documents");
  gtk_stack_set_hhomogeneous(GTK_STACK(content_), false);
  gtk_stack_set_vhomogeneous(GTK_STACK(content_), false);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_), workspace->create_pages(content_));
  adw_tab_overview_set_child(overview, toolbar_);
  root_ = workspace->create_root(GTK_WIDGET(overview));
  auto* switcher_host = gtk_overlay_new(); gtk_overlay_set_child(GTK_OVERLAY(switcher_host), root_);
  tab_switcher_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(tab_switcher_, "tab-switcher");
  auto* switcher_clamp = adw_clamp_new(); adw_clamp_set_maximum_size(ADW_CLAMP(switcher_clamp), 560); adw_clamp_set_tightening_threshold(ADW_CLAMP(switcher_clamp), 560);
  adw_clamp_set_child(ADW_CLAMP(switcher_clamp), tab_switcher_); gtk_widget_set_valign(switcher_clamp, GTK_ALIGN_CENTER);
  for (auto setter : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) setter(switcher_clamp, 24);
  gtk_widget_set_size_request(tab_switcher_, 280, -1);
  auto* switcher_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(switcher_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(switcher_scroll), 360);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(switcher_scroll), true);
  tab_switch_grid_ = GTK_FLOW_BOX(gtk_flow_box_new());
  gtk_flow_box_set_homogeneous(tab_switch_grid_, true);
  gtk_flow_box_set_min_children_per_line(tab_switch_grid_, 1); gtk_flow_box_set_max_children_per_line(tab_switch_grid_, 3);
  gtk_flow_box_set_row_spacing(tab_switch_grid_, 8); gtk_flow_box_set_column_spacing(tab_switch_grid_, 8);
  gtk_flow_box_set_selection_mode(tab_switch_grid_, GTK_SELECTION_SINGLE);
  gtk_flow_box_set_activate_on_single_click(tab_switch_grid_, true);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(switcher_scroll), GTK_WIDGET(tab_switch_grid_));
  gtk_box_append(GTK_BOX(tab_switcher_), switcher_scroll);
  gtk_overlay_add_overlay(GTK_OVERLAY(switcher_host), switcher_clamp);
  g_object_bind_property(tab_switcher_, "visible", switcher_clamp, "visible", G_BINDING_SYNC_CREATE); gtk_widget_set_visible(tab_switcher_, false);
  g_object_set_data(G_OBJECT(window), "tab-switcher", tab_switcher_);
  g_object_set_data(G_OBJECT(window), "tab-switcher-grid", tab_switch_grid_);
  g_signal_connect(tab_switch_grid_, "child-activated", G_CALLBACK(+[](GtkFlowBox*, GtkFlowBoxChild* child, Window* self) {
    self->tab_switch_index_ = gtk_flow_box_child_get_index(child); self->finish_tab_cycle(true);
  }), this);
  auto* switcher_keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(switcher_keys, GTK_PHASE_CAPTURE);
  g_signal_connect(switcher_keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType state, Window* self) -> gboolean {
    if (adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(self->window))) return false;
    if ((state & GDK_CONTROL_MASK) && !(state & (GDK_ALT_MASK | GDK_SUPER_MASK)) && (key == GDK_KEY_Tab || key == GDK_KEY_ISO_Left_Tab)) {
      self->cycle_tabs((state & GDK_SHIFT_MASK) || key == GDK_KEY_ISO_Left_Tab ? -1 : 1); return true;
    }
    if (!gtk_widget_get_visible(self->tab_switcher_)) return false;
    if (key == GDK_KEY_Escape) self->finish_tab_cycle(false);
    else if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) self->finish_tab_cycle(true);
    else if (key == GDK_KEY_Left) self->cycle_tabs(-1);
    else if (key == GDK_KEY_Right) self->cycle_tabs(1);
    else if (key == GDK_KEY_Up || key == GDK_KEY_Down) {
      const int tile_width = gtk_widget_get_width(GTK_WIDGET(gtk_flow_box_get_child_at_index(self->tab_switch_grid_, 0)));
      const int columns = std::max(1, (gtk_widget_get_width(GTK_WIDGET(self->tab_switch_grid_))+8)/(tile_width+8));
      self->cycle_tabs(key == GDK_KEY_Up ? -columns : columns);
    }
    return true;
  }), this);
  g_signal_connect(switcher_keys, "key-released", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType, Window* self) {
    if (key == GDK_KEY_Control_L || key == GDK_KEY_Control_R) self->finish_tab_cycle(true);
  }), this);
  gtk_widget_add_controller(GTK_WIDGET(window), switcher_keys);
  g_object_set_data(G_OBJECT(window), "tab-switcher-keys", switcher_keys);
  g_signal_connect(window, "notify::is-active", G_CALLBACK(+[](GObject*, GParamSpec*, Window* self) {
    if (!gtk_window_is_active(self->window)) self->finish_tab_cycle(false);
  }), this);
  auto* switcher_outside = gtk_gesture_click_new();
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(switcher_outside), GTK_PHASE_CAPTURE);
  g_signal_connect(switcher_outside, "pressed", G_CALLBACK(+[](GtkGestureClick*, int, double x, double y, Window* self) {
    if (!gtk_widget_get_visible(self->tab_switcher_)) return;
    auto* picked = gtk_widget_pick(GTK_WIDGET(self->window), x, y, GTK_PICK_DEFAULT);
    for (auto* node = picked; node; node = gtk_widget_get_parent(node)) if (node == self->tab_switcher_) return;
    self->finish_tab_cycle(false);
  }), this);
  gtk_widget_add_controller(GTK_WIDGET(window), GTK_EVENT_CONTROLLER(switcher_outside));
  toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new()); adw_toast_overlay_set_child(toasts, switcher_host);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), GTK_WIDGET(toasts));
  AdwBreakpoint* breakpoint = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 600sp"));
  GValue value = G_VALUE_INIT;
  g_value_init(&value, G_TYPE_STRING); g_value_set_string(&value, "title");
  adw_breakpoint_add_setter(breakpoint, G_OBJECT(header_content_), "visible-child-name", &value);
  g_value_unset(&value); adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window), breakpoint);
  add_actions();
  GMenu* tab_menu = g_menu_new();
  section(tab_menu, {{"Copy Link", "win.tab-copy"}, {"Reload", "win.tab-reload"}, {"Move to New Window", "win.tab-detach"}});
  section(tab_menu, {{"Close Tab", "win.tab-close"}, {"Close Other Tabs", "win.tab-close-others"}});
  adw_tab_view_set_menu_model(tabs, G_MENU_MODEL(tab_menu)); g_object_unref(tab_menu);
  g_signal_connect(tabs, "setup-menu", G_CALLBACK(+[](AdwTabView*, AdwTabPage* page, Window* self) {
    self->context_page_ = page;
    const bool enabled = page != nullptr;
    for (const auto* name : {"tab-copy", "tab-reload", "tab-detach", "tab-close", "tab-close-others"}) set_enabled(self->window, name, enabled);
  }), this);
  g_signal_connect(tabs, "close-page", G_CALLBACK(+[](AdwTabView*, AdwTabPage* page, Window* self) -> gboolean {
    auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"));
    if (browser) browser->close(); else adw_tab_view_close_page_finish(self->tabs, page, true);
    return true;
  }), this);
  g_signal_connect(tabs, "notify::selected-page", G_CALLBACK(+[](GObject*, GParamSpec*, Window* self) {
    if (self->adding_page_ || self->closing) return;
    if (self->finishing_tab_close_ && !std::strcmp(gtk_stack_get_visible_child_name(GTK_STACK(self->content_)), "home")) { self->update(); return; }
    if (adw_tab_view_get_selected_page(self->tabs)) gtk_stack_set_visible_child_name(GTK_STACK(self->content_), "documents");
    if (auto browser = self->current()) {
      if (!browser->loaded && self->app.cef_ready) browser->start();
      gtk_widget_grab_focus(browser->surface());
    }
    self->update();
  }), this);
  g_signal_connect(tabs, "page-attached", G_CALLBACK(+[](AdwTabView*, AdwTabPage* page, int, Window* self) {
    auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"));
    if (browser && std::none_of(self->browsers_.begin(), self->browsers_.end(), [browser](const auto& b) { return b.get() == browser; })) {
      self->browsers_.push_back(browser); browser->detach_callbacks();
    }
  }), this);
  g_signal_connect(tabs, "page-reordered", G_CALLBACK(+[](AdwTabView*, AdwTabPage*, int, Window* self) { self->update(); }), this);
  g_signal_connect(tabs, "page-detached", G_CALLBACK(+[](AdwTabView*, AdwTabPage* page, int, Window* self) {
    auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"));
    if (self->context_page_ == page) self->context_page_ = nullptr;
    if (browser) self->browsers_.erase(std::remove_if(self->browsers_.begin(), self->browsers_.end(), [browser](const auto& b) { return b.get() == browser; }), self->browsers_.end());
    if (!self->closing && !adw_tab_view_get_n_pages(self->tabs)) self->native_home();
    else self->update();
  }), this);
  gtk_widget_add_tick_callback(GTK_WIDGET(window), +[](GtkWidget* widget, GdkFrameClock*, gpointer p) -> gboolean {
    auto* self = static_cast<Window*>(p);
    if (!gtk_widget_get_width(widget)) return G_SOURCE_CONTINUE;
    // The first header update runs before the window has an allocation. Select
    // the tab strip once its real width is known, then stop the frame callback.
    self->update(); return G_SOURCE_REMOVE;
  }, this, nullptr);
  g_signal_connect(tabs, "create-window", G_CALLBACK(+[](AdwTabView*, Window* self) -> AdwTabView* { return self->app.new_window()->tabs; }), this);
  g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow*, Window* self) -> gboolean { self->close(); return true; }), this);
  g_signal_connect(window, "notify::fullscreened", G_CALLBACK(+[](GObject*, GParamSpec*, Window* self) {
    adw_toolbar_view_set_reveal_top_bars(ADW_TOOLBAR_VIEW(self->toolbar_), !gtk_window_is_fullscreen(self->window));
  }), this);
  GtkEventController* edge = gtk_event_controller_motion_new();
  g_signal_connect(edge, "motion", G_CALLBACK(+[](GtkEventControllerMotion*, double, double y, Window* self) {
    if (gtk_window_is_fullscreen(self->window)) adw_toolbar_view_set_reveal_top_bars(ADW_TOOLBAR_VIEW(self->toolbar_), y < 64);
  }), this);
  gtk_event_controller_set_propagation_phase(edge, GTK_PHASE_CAPTURE); gtk_widget_add_controller(GTK_WIDGET(window), edge);
  rebuild_home(); update();
}
Window::~Window() { g_object_unref(capture_); if (!destroyed) gtk_window_destroy(window); g_object_unref(window); }
CefRefPtr<Browser> Window::current() const {
  if (auth_browser_ && !auth_browser_->closing && !std::strcmp(gtk_stack_get_visible_child_name(GTK_STACK(content_)), "home")) return auth_browser_;
  if (home_browser_ && !std::strcmp(gtk_stack_get_visible_child_name(GTK_STACK(content_)), "home")) return home_browser_;
  AdwTabPage* page = adw_tab_view_get_selected_page(tabs);
  return page ? static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser")) : nullptr;
}
void Window::attach(CefRefPtr<Browser> browser, bool activate) {
  adding_page_ = true;
  AdwTabPage* page = adw_tab_view_append(tabs, browser->widget());
  adding_page_ = false;
  adw_tab_page_set_title(page, browser->name.c_str()); adw_tab_page_set_tooltip(page, browser->name.c_str());
  adw_tab_page_set_icon(page, nullptr);
  if (activate) { adw_tab_view_set_selected_page(tabs, page); gtk_stack_set_visible_child_name(GTK_STACK(content_), "documents"); }
}
CefRefPtr<Browser> Window::open(const std::string& url, bool activate, bool defer) {
  if (Policy::is_figma(url) && !Policy::is_file(url) && !Policy::is_new_tab(url)) {
    native_home();
    if (home_browser_->url != url) home_browser_->navigate(url);
    return home_browser_;
  }
  if (activate) workspace->show_overview(false);
  const std::string key = Policy::file_key(url);
  for (auto& b : browsers_) if (!Policy::is_new_tab(url) && (b->url == url || (!key.empty() && Policy::file_key(b->url) == key))) {
    if (activate) {
      if (b->is_home) native_home();
      else { adw_tab_view_set_selected_page(tabs, adw_tab_view_get_page(tabs, b->widget())); gtk_stack_set_visible_child_name(GTK_STACK(content_), "documents"); }
    }
    update();
    return b;
  }
  CefRefPtr<Browser> browser = new Browser(url, app.software);
  if (Policy::is_new_tab(url)) browser->name = "New File";
  attach(browser, activate);
  if (!defer && app.cef_ready && gtk_widget_get_mapped(browser->surface())) browser->start();
  update(); return browser;
}
CefRefPtr<Browser> Window::popup(const std::string& url) {
  CefRefPtr<Browser> popup = new Browser(url, app.software);
  popup->loaded = true; popup->is_auth_popup = true; popup->is_home = true;
  if (!home_) rebuild_home();
  browsers_.push_back(popup); auth_browser_ = popup;
  gtk_stack_add_child(GTK_STACK(home_), popup->widget());
  gtk_stack_set_visible_child(GTK_STACK(home_), popup->widget());
  gtk_stack_set_visible_child_name(GTK_STACK(content_), "home");
  update(); return popup;
}
void Window::update() {
  if (destroyed) return;
  auto browser = current();
  std::erase_if(tab_mru_, [&](Browser* tab) {
    return std::none_of(browsers_.begin(), browsers_.end(), [&](const auto& b) { return b.get() == tab && !b->closing && !b->is_auth_popup; });
  });
  if (browser && !browser->closing && !browser->is_auth_popup && (tab_mru_.empty() || tab_mru_.front() != browser.get())) {
    std::erase(tab_mru_, browser.get()); tab_mru_.insert(tab_mru_.begin(), browser.get());
  }
  if (!tab_switch_order_.empty() && (browser != tab_switch_origin_ || std::any_of(tab_switch_order_.begin(), tab_switch_order_.end(), [this](const auto& b) { return b->closing || b->owner() != this; }))) finish_tab_cycle(false);
  const char* title = browser && !browser->is_home ? browser->name.c_str() : "Select for Figma";
  adw_window_title_set_title(title_, title); gtk_window_set_title(window, zane_account ? "Zane Account — Select for Figma" : title);
  adw_window_title_set_subtitle(title_, browser && browser->loading ? "Loading…" : "");
  set_enabled(window, "back", browser && browser->can_back()); set_enabled(window, "forward", browser && browser->can_forward());
  for (const auto* name : {"reload", "hard-reload"}) set_enabled(window, name, browser != nullptr);
  set_enabled(window, "copy-link", browser && Policy::is_file(browser->url));
  set_enabled(window, "external", browser && !browser->is_auth_popup && !Policy::is_sign_in(browser->url));
  set_enabled(window, "paste", browser != nullptr);
  set_enabled(window, "keepassxc-fill", browser && !KeePassXC::credential_url(browser->url).empty());
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(home_button_), browser && browser->is_home);
  if (browser && browser->is_home) gtk_widget_add_css_class(tab_bar_, "home-active");
  else gtk_widget_remove_css_class(tab_bar_, "home-active");
  gtk_stack_set_visible_child_name(GTK_STACK(header_content_), adw_tab_view_get_n_pages(tabs) && gtk_widget_get_width(GTK_WIDGET(window)) > 600 ? "tabs" : "title");
  gtk_widget_set_visible(reload_, browser != nullptr);
  for (auto& b : browsers_) {
    if (b->is_home) continue;
    AdwTabPage* page = adw_tab_view_get_page(tabs, b->widget());
    if (!page) continue;
    adw_tab_page_set_title(page, b->name.c_str()); adw_tab_page_set_tooltip(page, b->name.c_str());
    adw_tab_page_set_loading(page, b->loading);
  }
  workspace->update();
  if (!closing && !zane_account) { app.persist(); app.queue_save(); }
}
void Window::persist() {
  if (zane_account) return;
  if (!gtk_window_is_maximized(window) && !gtk_window_is_fullscreen(window)) gtk_window_get_default_size(window, &app.state.width, &app.state.height);
  app.state.maximized = gtk_window_is_maximized(window);
  app.persist(); app.state.save();
}
void Window::browser_closed(Browser* browser) {
  if (browser->is_auth_popup) {
    gtk_stack_remove(GTK_STACK(home_), browser->widget());
    browsers_.erase(std::remove_if(browsers_.begin(), browsers_.end(), [browser](const auto& b) { return b.get() == browser; }), browsers_.end());
    auth_browser_ = nullptr;
    for (auto i = browsers_.rbegin(); i != browsers_.rend(); ++i) if ((*i)->is_auth_popup && !(*i)->closing) { auth_browser_ = *i; break; }
    if (auth_browser_) gtk_stack_set_visible_child(GTK_STACK(home_), auth_browser_->widget());
    else if (home_browser_) {
      gtk_stack_set_visible_child(GTK_STACK(home_), home_browser_->widget());
      // The opener was hidden during sign-in, so refresh its session after the
      // popup has finished running its callback and actually closed.
      if (!closing && refresh_after_auth_) {
        struct Refresh { CefRefPtr<Browser> home; std::string url; };
        // Allow queued postMessage events to reach the opener before reloading it.
        g_timeout_add_full(G_PRIORITY_DEFAULT, 250, +[](gpointer p) -> gboolean {
          auto* refresh = static_cast<Refresh*>(p); auto home = refresh->home;
          auto* owner = home->owner();
          if (owner && !owner->closing && !owner->auth_browser_ && !home->closing && home->url == refresh->url) {
            if (owner->app.auth_test || !Policy::is_sign_in(home->url)) home->reload();
            else home->navigate(Policy::home_url);
          }
          return G_SOURCE_REMOVE;
        }, new Refresh{home_browser_, home_browser_->url}, +[](gpointer p) { delete static_cast<Refresh*>(p); });
      }
      refresh_after_auth_ = true;
    }
  }
  if (browser == home_browser_.get()) {
    gtk_stack_remove(GTK_STACK(home_), browser->widget());
    browsers_.erase(std::remove_if(browsers_.begin(), browsers_.end(), [browser](const auto& b) { return b.get() == browser; }), browsers_.end());
    home_browser_ = nullptr;
  }
  AdwTabPage* page = browser->is_home ? nullptr : adw_tab_view_get_page(tabs, browser->widget());
  if (page) {
    if (!browser->closing) adw_tab_view_close_page(tabs, page);
    else { finishing_tab_close_ = true; adw_tab_view_close_page_finish(tabs, page, true); finishing_tab_close_ = false; }
  }
  if (closing && browsers_.empty()) {
    destroyed = true; gtk_window_destroy(window);
  } else update();
}
void Window::finish_auth(Browser* browser) {
  if (closing || destroyed || !browser->is_auth_popup || browser->closing) return;
  const auto destination = Policy::is_file(browser->url) ? browser->url : std::string();
  browser->close();
  if (!destination.empty()) app.open(destination);
}
void Window::close() {
  if (closing || destroyed) return;
  persist(); closing = true;
  auto copy = browsers_;
  for (auto& browser : copy) {
    if (browser->is_home) { browser->close(); continue; }
    AdwTabPage* page = adw_tab_view_get_page(tabs, browser->widget());
    if (page) adw_tab_view_close_page(tabs, page);
  }
  if (!zane_account && std::none_of(app.windows.begin(),app.windows.end(),[](const auto& window){return !window->zane_account && !window->closing && !window->destroyed;})) {
    for (auto& window : app.windows) if (window->zane_account) window->close();
  }
  if (browsers_.empty()) { destroyed = true; gtk_window_destroy(window); }
}
void Window::toast(const std::string& message) { if (!destroyed) adw_toast_overlay_add_toast(toasts, adw_toast_new(message.c_str())); }
void Window::cycle_tabs(int direction) {
  if (closing || destroyed) return;
  if (tab_switch_order_.empty()) {
    if (home_browser_ && !home_browser_->closing) tab_switch_order_.push_back(home_browser_);
    for (int i = 0; i < adw_tab_view_get_n_pages(tabs); ++i) {
      auto* tab = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(tabs, i))), "browser"));
      if (tab && !tab->closing) tab_switch_order_.push_back(tab);
    }
    if (tab_switch_order_.size() < 2) { tab_switch_order_.clear(); return; }
    // Freeze recency for this gesture; highlighting must not reorder the list.
    if (app.state.tab_switcher_mru) std::stable_sort(tab_switch_order_.begin(), tab_switch_order_.end(), [this](const auto& a, const auto& b) {
      return std::find(tab_mru_.begin(), tab_mru_.end(), a.get()) < std::find(tab_mru_.begin(), tab_mru_.end(), b.get());
    });
    tab_switch_origin_ = current();
    auto selected = std::find(tab_switch_order_.begin(), tab_switch_order_.end(), tab_switch_origin_);
    tab_switch_index_ = selected == tab_switch_order_.end() ? 0 : selected-tab_switch_order_.begin();
    gtk_flow_box_remove_all(tab_switch_grid_);
    auto* scroll = GTK_SCROLLED_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(tab_switch_grid_), GTK_TYPE_SCROLLED_WINDOW));
    gtk_scrolled_window_set_max_content_height(scroll, std::clamp(gtk_widget_get_height(GTK_WIDGET(window))-160, 140, 360));
    for (const auto& tab : tab_switch_order_) {
      auto* tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
      gtk_box_append(GTK_BOX(tile), WorkspaceTabs::thumbnail(tab, 160, 100));
      auto* label = gtk_label_new(tab->is_home ? "Home" : tab->name.c_str());
      gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(label), 18);
      gtk_box_append(GTK_BOX(tile), label); gtk_flow_box_append(tab_switch_grid_, tile);
      gtk_widget_set_tooltip_text(gtk_widget_get_parent(tile), tab->is_home ? "Home" : tab->name.c_str());
    }
    workspace->show_overview(false); gtk_widget_set_visible(tab_switcher_, true);
  }
  const int count = tab_switch_order_.size();
  tab_switch_index_ = (static_cast<int>(tab_switch_index_)+direction%count+count)%count;
  auto* child = gtk_flow_box_get_child_at_index(tab_switch_grid_, tab_switch_index_);
  gtk_flow_box_select_child(tab_switch_grid_, child); gtk_widget_grab_focus(GTK_WIDGET(child));
  // Wait for new tiles to be allocated before revealing the selected preview.
  if (tab_switch_scroll_tick_) gtk_widget_remove_tick_callback(GTK_WIDGET(tab_switch_grid_), tab_switch_scroll_tick_);
  tab_switch_scroll_tick_ = gtk_widget_add_tick_callback(GTK_WIDGET(tab_switch_grid_), +[](GtkWidget* grid, GdkFrameClock*, gpointer data) -> gboolean {
    auto* self = static_cast<Window*>(data);
    auto* child = GTK_WIDGET(gtk_flow_box_get_child_at_index(self->tab_switch_grid_, self->tab_switch_index_));
    if (gtk_widget_get_height(child) == 0) return G_SOURCE_CONTINUE;
    graphene_rect_t bounds;
    if (gtk_widget_compute_bounds(child, grid, &bounds)) {
      auto* scroll = GTK_SCROLLED_WINDOW(gtk_widget_get_ancestor(grid, GTK_TYPE_SCROLLED_WINDOW));
      gtk_adjustment_clamp_page(gtk_scrolled_window_get_vadjustment(scroll), bounds.origin.y, bounds.origin.y+bounds.size.height);
    }
    self->tab_switch_scroll_tick_ = 0; return G_SOURCE_REMOVE;
  }, this, nullptr);
}
void Window::finish_tab_cycle(bool activate) {
  if (tab_switch_order_.empty()) return;
  auto selected = tab_switch_order_[tab_switch_index_];
  tab_switch_order_.clear(); tab_switch_origin_ = nullptr;
  if (tab_switch_scroll_tick_) { gtk_widget_remove_tick_callback(GTK_WIDGET(tab_switch_grid_), tab_switch_scroll_tick_); tab_switch_scroll_tick_ = 0; }
  gtk_widget_set_visible(tab_switcher_, false);
  if (closing || destroyed) return;
  if (activate && !selected->closing && selected->owner() == this) workspace->activate(selected);
  else if (auto browser = current()) gtk_widget_grab_focus(browser->surface());
}
void Window::native_home() {
  if (!home_browser_) rebuild_home();
  if (home_browser_) gtk_stack_set_visible_child_name(GTK_STACK(content_), "home");
  workspace->show_overview(false); update();
}
void Window::refresh_home() { if (!destroyed) rebuild_home(); }
void Window::rebuild_home() {
  if (home_browser_) return;
  if (!home_) {
    home_ = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(home_), false); gtk_stack_set_vhomogeneous(GTK_STACK(home_), false);
    gtk_stack_add_named(GTK_STACK(content_), home_, "home");
  }
  CefRefPtr<Browser> browser = new Browser(Policy::home_url, app.software);
  browser->is_home = true;
  browser->name = "Home";
  home_browser_ = browser; browsers_.push_back(browser);
  gtk_stack_add_named(GTK_STACK(home_), browser->widget(), "files");
  gtk_stack_set_visible_child_name(GTK_STACK(home_), "files");
  gtk_stack_set_visible_child_name(GTK_STACK(content_), "home");
}
void Window::add_actions() {
  auto* map = G_ACTION_MAP(window);
  action(map, "open-link", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->open_link_dialog(); }), this);
  action(map, "new-tab", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    w->workspace->new_tab();
  }), this);
  action(map, "new-tab-group", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->workspace->name_dialog(nullptr); }), this);
  action(map, "figma", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->open(Policy::home_url); }), this);
  action(map, "home", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->native_home(); }), this);
  action(map, "new-window", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->app.new_window(); }), this);
  action(map, "close-window", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->close(); }), this);
  action(map, "close-tab", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    AdwTabPage* page = adw_tab_view_get_selected_page(w->tabs);
    if (auto browser = w->current(); browser && browser->is_home) w->close(); else if (page) adw_tab_view_close_page(w->tabs, page);
  }), this);
  action(map, "reload", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current()) b->reload(); }), this);
  action(map, "hard-reload", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current(); b && b->cef_browser()) b->cef_browser()->ReloadIgnoreCache(); }), this);
  action(map, "back", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current()) b->back(); }), this);
  action(map, "forward", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current()) b->forward(); }), this);
  for (const auto* name : {"next-tab", "previous-tab"}) action(map, name, G_CALLBACK(+[](GSimpleAction* action, GVariant*, Window* w) {
    const int count = adw_tab_view_get_n_pages(w->tabs); if (!count) return;
    const int index = adw_tab_view_get_page_position(w->tabs, adw_tab_view_get_selected_page(w->tabs));
    const int direction = !std::strcmp(g_action_get_name(G_ACTION(action)), "next-tab") ? 1 : -1;
    auto* page = adw_tab_view_get_nth_page(w->tabs, (index+direction+count)%count);
    w->workspace->activate(static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser")));
  }), this);
  action(map, "overview", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->workspace->show_overview(!w->workspace->overview_open()); }), this);
  action(map, "copy-link", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    if (auto b = w->current()) { gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(w->window)), b->url.c_str()); w->toast("Link copied"); }
  }), this);
  action(map, "paste", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current()) Clipboard::paste(*b); }), this);
  action(map, "keepassxc-fill", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->fill_keepassxc(); }), this);
  action(map, "external", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (auto b = w->current()) w->app.external(b->url); }), this);
  action(map, "downloads", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->show_downloads(); }), this);
  action(map, "preferences", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->show_preferences(); }), this);
  action(map, "about", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->show_about(); }), this);
  action(map, "shortcuts", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->show_shortcuts(); }), this);
  action(map, "fullscreen", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { w->toggle_fullscreen(); }), this);
  action(map, "tab-copy", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    if (!w->context_page_) return;
    auto* b = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(w->context_page_)), "browser"));
    if (b) { gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(w->window)), b->url.c_str()); w->toast("Link copied"); }
  }), this);
  action(map, "tab-group-new", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    if (!w->context_page_) return;
    auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(w->context_page_)), "browser"));
    w->workspace->name_dialog(browser);
  }), this);
  for (const auto* name : {"tab-group-move", "group-rename", "group-color", "group-toggle", "group-ungroup"}) {
    GSimpleAction* a = g_simple_action_new(name, G_VARIANT_TYPE_STRING);
    g_signal_connect(a, "activate", G_CALLBACK(+[](GSimpleAction* action, GVariant* value, Window* w) {
      const std::string id = g_variant_get_string(value, nullptr);
      const std::string name = g_action_get_name(G_ACTION(action));
      if (name == "group-rename") w->workspace->name_dialog(nullptr, id);
      else if (name == "group-color") w->workspace->color_dialog(id);
      else if (name == "group-toggle") w->workspace->toggle_group(id);
      else if (name == "group-ungroup") w->workspace->ungroup(id);
      else if (w->context_page_) {
        auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(w->context_page_)), "browser"));
        w->workspace->assign(browser, id);
      }
    }), this);
    g_action_map_add_action(map, G_ACTION(a)); g_object_unref(a);
  }
  action(map, "tab-reload", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    if (w->context_page_) if (auto* b = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(w->context_page_)), "browser"))) b->reload();
  }), this);
  action(map, "tab-detach", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) {
    if (!w->context_page_) return;
    Window* other = w->app.new_window(); adw_tab_view_transfer_page(w->tabs, w->context_page_, other->tabs, adw_tab_view_get_n_pages(other->tabs));
  }), this);
  action(map, "tab-close", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (w->context_page_) adw_tab_view_close_page(w->tabs, w->context_page_); }), this);
  action(map, "tab-close-others", G_CALLBACK(+[](GSimpleAction*, GVariant*, Window* w) { if (w->context_page_) adw_tab_view_close_other_pages(w->tabs, w->context_page_); }), this);
}
void Window::toggle_fullscreen() { if (gtk_window_is_fullscreen(window)) gtk_window_unfullscreen(window); else gtk_window_fullscreen(window); }
void Window::open_link_dialog() {
  AdwDialog* dialog = adw_alert_dialog_new("Open Figma Link", "Paste a link to a Figma file, FigJam board, or presentation.");
  GtkWidget* entry = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "https://www.figma.com/design/…");
  gtk_accessible_update_property(GTK_ACCESSIBLE(entry), GTK_ACCESSIBLE_PROPERTY_LABEL, "Figma Link", -1);
  gtk_entry_set_activates_default(GTK_ENTRY(entry), true);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "open", "_Open");
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "open", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "open"); adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  adw_alert_dialog_set_response_enabled(ADW_ALERT_DIALOG(dialog), "open", false);
  g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable* entry, AdwAlertDialog* dialog) {
    const bool valid = !Policy::normalize(gtk_editable_get_text(entry)).empty();
    adw_alert_dialog_set_response_enabled(dialog, "open", valid);
    if (valid || !*gtk_editable_get_text(entry)) gtk_widget_remove_css_class(GTK_WIDGET(entry), "error"); else gtk_widget_add_css_class(GTK_WIDGET(entry), "error");
  }), dialog);
  g_object_set_data(G_OBJECT(dialog), "entry", entry);
  g_signal_connect(dialog, "response", G_CALLBACK(+[](AdwAlertDialog* dialog, const char* response, Window* self) {
    if (!std::strcmp(response, "open")) self->app.open(gtk_editable_get_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "entry"))));
  }), this);
  adw_dialog_present(dialog, GTK_WIDGET(window)); gtk_widget_grab_focus(entry);
}
void Window::show_preferences() {
  AdwDialog* dialog = adw_preferences_dialog_new(); adw_dialog_set_title(dialog, "Preferences");
  GtkWidget* page = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "General"); adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), "preferences-system-symbolic");
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));
  Zane::add_preferences(*this,ADW_PREFERENCES_PAGE(page));
  GtkWidget* appearance = adw_preferences_group_new(); adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(appearance), "Appearance");
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(appearance));
  GtkWidget* style = adw_combo_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(style), "Style");
  const char* choices[] = {"Follow System", "Light", "Dark", nullptr}; GtkStringList* model = gtk_string_list_new(choices);
  adw_combo_row_set_model(ADW_COMBO_ROW(style), G_LIST_MODEL(model)); g_object_unref(model);
  adw_combo_row_set_selected(ADW_COMBO_ROW(style), app.state.appearance == "light" ? 1 : app.state.appearance == "dark" ? 2 : 0);
  g_signal_connect(style, "notify::selected", G_CALLBACK((+[](AdwComboRow* row, GParamSpec*, Window* self) {
    const char* values[] = {"system", "light", "dark"}; self->app.state.appearance = values[adw_combo_row_get_selected(row)];
    self->app.apply_appearance(); self->app.state.save();
  })), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), style);
  GtkWidget* editor_theme = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(editor_theme), "Adwaita Editor Theme");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(editor_theme), "GNOME styling for Figma’s homepage, panels and controls, inspired by Rnote");
  adw_switch_row_set_active(ADW_SWITCH_ROW(editor_theme), app.state.adwaita_editor);
  g_signal_connect(editor_theme, "notify::active", G_CALLBACK(+[](AdwSwitchRow* row, GParamSpec*, Window* self) {
    self->app.state.adwaita_editor = adw_switch_row_get_active(row);
    for (auto& window : self->app.windows) for (auto& browser : window->browsers_) browser->refresh_theme();
    self->app.state.save();
  }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), editor_theme);
  GtkWidget* behavior = adw_preferences_group_new(); adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(behavior), "Workspace");
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(behavior));
  GtkWidget* tab_order = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tab_order), "Ctrl+Tab Order");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(tab_order), "Include Home when cycling through tabs");
  const char* tab_orders[] = {"Tab position", "Most recently used", nullptr};
  auto* tab_order_model = gtk_string_list_new(tab_orders);
  adw_combo_row_set_model(ADW_COMBO_ROW(tab_order), G_LIST_MODEL(tab_order_model)); g_object_unref(tab_order_model);
  adw_combo_row_set_selected(ADW_COMBO_ROW(tab_order), app.state.tab_switcher_mru ? 1 : 0);
  g_signal_connect(tab_order, "notify::selected", G_CALLBACK(+[](AdwComboRow* row, GParamSpec*, Window* self) {
    self->app.state.tab_switcher_mru = adw_combo_row_get_selected(row) == 1; self->app.state.save();
  }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior), tab_order);
  GtkWidget* restore = adw_switch_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(restore), "Restore Open Files");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(restore), "Reopen document tabs when the app starts");
  adw_switch_row_set_active(ADW_SWITCH_ROW(restore), app.state.restore_session);
  g_signal_connect(restore, "notify::active", G_CALLBACK(+[](AdwSwitchRow* row, GParamSpec*, Window* self) { self->app.state.restore_session = adw_switch_row_get_active(row); self->app.state.save(); }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior), restore);
  GtkWidget* fonts = adw_switch_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fonts), "Local Fonts");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(fonts), "Make installed fonts available to Figma"); adw_switch_row_set_active(ADW_SWITCH_ROW(fonts), app.state.local_fonts);
  g_signal_connect(fonts, "notify::active", G_CALLBACK(+[](AdwSwitchRow* row, GParamSpec*, Window* self) {
    self->app.state.local_fonts = adw_switch_row_get_active(row);
    if (self->app.state.local_fonts) self->app.fonts = std::make_unique<Fonts>(); else self->app.fonts.reset();
    for (auto& window : self->app.windows) for (auto& browser : window->browsers_) browser->set_permissions();
    self->app.state.save();
  }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior), fonts);
  GtkWidget* notify = adw_switch_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(notify), "Notifications");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(notify), "Allow Figma notifications and background download alerts"); adw_switch_row_set_active(ADW_SWITCH_ROW(notify), app.state.notifications);
  g_signal_connect(notify, "notify::active", G_CALLBACK(+[](AdwSwitchRow* row, GParamSpec*, Window* self) {
    self->app.state.notifications = adw_switch_row_get_active(row);
    for (auto& window : self->app.windows) for (auto& browser : window->browsers_) browser->set_permissions();
    self->app.state.save();
  }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(behavior), notify);
  GtkWidget* passwords = adw_preferences_group_new(); adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(passwords), "Passwords");
  adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(passwords), "Enable Browser Integration in KeePassXC, then open and unlock your database.");
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(passwords));
  GtkWidget* connection = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(connection), "KeePassXC");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(connection), app.state.keepassxc_paired ? "Connected · pairing stored in the system keyring" : "Not connected");
  GtkWidget* connect = gtk_button_new_with_label(app.state.keepassxc_paired ? "Reconnect" : "Connect");
  gtk_widget_set_valign(connect, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(connection), connect);
  g_object_set_data(G_OBJECT(connect), "connection-row", connection);
  g_object_set_data(G_OBJECT(connection), "connect-button", connect);
  g_signal_connect(connect, "clicked", G_CALLBACK(+[](GtkButton* button, Window* self) {
    self->pair_keepassxc(GTK_WIDGET(g_object_get_data(G_OBJECT(button), "connection-row")));
  }), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(passwords), connection);
  if (app.state.keepassxc_paired) {
    GtkWidget* forget = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(forget), "Forget Connection");
    GtkWidget* button = gtk_button_new_with_label("Forget"); gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(forget), button); adw_preferences_group_add(ADW_PREFERENCES_GROUP(passwords), forget);
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, Window* self) {
      gtk_widget_set_sensitive(GTK_WIDGET(button), false); g_object_ref(button);
      g_application_hold(G_APPLICATION(self->app.gtk_app));
      KeePassXC::forget([self, button](KeePassXC::Result result) {
        if (result.error.empty()) { self->app.state.keepassxc_paired = false; self->app.state.save(); }
        if (!self->destroyed) self->toast(result.error.empty() ? "KeePassXC connection forgotten" : result.error);
        g_object_unref(button); g_application_release(G_APPLICATION(self->app.gtk_app));
      });
    }), this);
  }
  adw_dialog_present(dialog, GTK_WIDGET(window));
}
void Window::pair_keepassxc(GtkWidget* row) {
  if (row) {
    g_object_ref(row);
    gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(row), "connect-button")), false);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Approve the connection in KeePassXC");
  }
  g_application_hold(G_APPLICATION(app.gtk_app));
  KeePassXC::pair([this, row](KeePassXC::Result result) {
    if (result.error.empty()) { app.state.keepassxc_paired = true; app.state.save(); }
    if (!destroyed) {
      if (row) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), result.error.empty() ? "Connected · pairing stored in the system keyring" : result.error.c_str());
        GtkWidget* button = GTK_WIDGET(g_object_get_data(G_OBJECT(row), "connect-button"));
        gtk_widget_set_sensitive(button, true); gtk_button_set_label(GTK_BUTTON(button), app.state.keepassxc_paired ? "Reconnect" : "Connect");
      }
      toast(result.error.empty() ? "Connected to KeePassXC" : result.error);
    }
    if (row) g_object_unref(row);
    g_application_release(G_APPLICATION(app.gtk_app));
  });
}
namespace {
void fill_login(CefRefPtr<Browser> browser, const std::string& url, const KeePassXC::Login& login) {
  if (browser->closing || browser->url != url || !browser->cef_browser() || !browser->owner()) return;
  if (KeePassXC::credential_url(url).empty()) return;
  JsonObject* values = json_object_new();
  json_object_set_string_member(values, "url", url.c_str());
  json_object_set_string_member(values, "username", login.username.c_str());
  json_object_set_string_member(values, "password", login.password.c_str());
  JsonNode* node = json_node_new(JSON_NODE_OBJECT); json_node_take_object(node, values);
  JsonGenerator* generator = json_generator_new(); json_generator_set_root(generator, node);
  char* data = json_generator_to_data(generator, nullptr);
  std::string script = std::string(R"JS((() => {const login = )JS") + data + R"JS(;
    if (location.href !== login.url) return;
    const visible = input => !input.disabled && !input.readOnly && input.getClientRects().length;
    const inputs = [...document.querySelectorAll('input')].filter(visible);
    const password = inputs.find(input => input.type === 'password' && input.autocomplete !== 'new-password');
    const username = inputs.find(input => input.type === 'email' || /username|email/.test(input.autocomplete)) ||
      inputs.find(input => input.type === 'text' && /email|user|login/i.test(input.name + input.id));
    const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    const fill = (input, value) => { if (!input) return; setter.call(input, value); input.dispatchEvent(new Event('input', {bubbles: true})); input.dispatchEvent(new Event('change', {bubbles: true})); };
    fill(username, login.username); fill(password, login.password);
    if (password || username) (password || username).focus();
  })())JS";
  browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(script, "figma-gtk://keepassxc", 1);
  sodium_memzero(script.data(), script.size()); sodium_memzero(data, std::strlen(data));
  g_free(data); g_object_unref(generator); json_node_free(node);
}
std::string search_text(const std::string& text) {
  char* folded = g_utf8_casefold(text.c_str(), -1);
  char* normalized = g_utf8_normalize(folded, -1, G_NORMALIZE_ALL_COMPOSE);
  std::string result = normalized ? normalized : folded;
  g_free(normalized); g_free(folded); return result;
}
struct LoginChoices {
  CefRefPtr<Browser> browser; std::string url; std::vector<KeePassXC::Login> entries; AdwDialog* dialog;
  std::vector<std::string> search; std::string query; GtkWidget* list = nullptr; GtkWidget* empty = nullptr;
};
}
void Window::fill_keepassxc() {
  auto browser = current();
  if (!browser || KeePassXC::credential_url(browser->url).empty()) return;
  if (!app.state.keepassxc_paired) { show_preferences(); toast("Connect to KeePassXC in Preferences first"); return; }
  const std::string source = browser->url;
  g_application_hold(G_APPLICATION(app.gtk_app));
  toast("Looking for a login in KeePassXC…");
  KeePassXC::get_logins(source, [this, browser, source](KeePassXC::Result result) {
    if (!destroyed && !browser->closing && browser->url == source) {
      if (!result.error.empty()) toast(result.error);
      else if (result.logins.empty()) toast("No matching login in KeePassXC");
      else show_login_picker(browser, source, std::move(result.logins));
    }
    g_application_release(G_APPLICATION(app.gtk_app));
  });
}
AdwDialog* Window::show_login_picker(CefRefPtr<Browser> browser, const std::string& source, std::vector<KeePassXC::Login> entries) {
  AdwDialog* dialog = adw_alert_dialog_new("Choose a Login", "Choose the KeePassXC login to fill on this page.");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  GtkWidget* list = gtk_list_box_new(); gtk_widget_add_css_class(list, "boxed-list"); gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  auto* choices = new LoginChoices{browser, source, std::move(entries), dialog, {}, {}};
  choices->list = list;
  GtkWidget* contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* search = gtk_search_entry_new();
  g_object_set(search, "placeholder-text", "Search logins", nullptr);
  gtk_accessible_update_property(GTK_ACCESSIBLE(search), GTK_ACCESSIBLE_PROPERTY_LABEL, "Search logins", -1);
  gtk_box_append(GTK_BOX(contents), search);
  choices->empty = gtk_label_new("No Matching Logins");
  gtk_widget_add_css_class(choices->empty, "dim-label");
  gtk_widget_set_margin_top(choices->empty, 24); gtk_widget_set_margin_bottom(choices->empty, 24);
  gtk_widget_set_visible(choices->empty, false);
  g_object_set_data(G_OBJECT(dialog), "login-search", search);
  g_object_set_data(G_OBJECT(dialog), "login-list", list);
  gtk_list_box_set_filter_func(GTK_LIST_BOX(list), +[](GtkListBoxRow* row, gpointer p) -> gboolean {
    auto* choices = static_cast<LoginChoices*>(p);
    const auto i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "login-index"));
    return choices->query.empty() || choices->search[i].find(choices->query) != std::string::npos;
  }, choices, nullptr);
  g_signal_connect(search, "search-changed", G_CALLBACK(+[](GtkSearchEntry* search, LoginChoices* choices) {
    choices->query = search_text(gtk_editable_get_text(GTK_EDITABLE(search)));
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(choices->list));
    const bool matches = std::any_of(choices->search.begin(), choices->search.end(), [choices](const auto& text) {
      return choices->query.empty() || text.find(choices->query) != std::string::npos;
    });
    gtk_widget_set_visible(choices->list, matches); gtk_widget_set_visible(choices->empty, !matches);
  }), choices);
  g_signal_connect(search, "activate", G_CALLBACK(+[](GtkSearchEntry*, LoginChoices* choices) {
    for (GtkWidget* row = gtk_widget_get_first_child(choices->list); row; row = gtk_widget_get_next_sibling(row))
      if (GTK_IS_LIST_BOX_ROW(row) && gtk_widget_get_child_visible(row)) { g_signal_emit_by_name(row, "activated"); break; }
  }), choices);
  g_object_set_data_full(G_OBJECT(dialog), "login-choices", choices, +[](gpointer p) { delete static_cast<LoginChoices*>(p); });
  for (unsigned i = 0; i < choices->entries.size(); ++i) {
    const auto& entry = choices->entries[i]; GtkWidget* row = adw_action_row_new();
    choices->search.push_back(search_text(entry.name + "\n" + entry.username));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), entry.name.empty() ? "Login" : entry.name.c_str());
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), false);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), entry.username.c_str()); gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), true);
    adw_action_row_add_prefix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("dialog-password-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("go-next-symbolic"));
    g_object_set_data(G_OBJECT(row), "login-index", GUINT_TO_POINTER(i));
    g_signal_connect(row, "activated", G_CALLBACK(+[](AdwActionRow* row, LoginChoices* choices) {
      const auto index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "login-index"));
      fill_login(choices->browser, choices->url, choices->entries[index]); adw_dialog_close(choices->dialog);
    }), choices);
    gtk_list_box_append(GTK_LIST_BOX(list), row);
  }
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), true);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
  gtk_box_append(GTK_BOX(contents), scroll); gtk_box_append(GTK_BOX(contents), choices->empty);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), contents);
  adw_dialog_set_focus(ADW_DIALOG(dialog), search);
  adw_dialog_present(dialog, GTK_WIDGET(window));
  return dialog;
}
void Window::show_downloads() {
  AdwDialog* dialog = adw_dialog_new(); adw_dialog_set_title(dialog, "Downloads"); adw_dialog_set_content_width(dialog, 600); adw_dialog_set_content_height(dialog, 520);
  GtkWidget* toolbar = adw_toolbar_view_new(); GtkWidget* header = adw_header_bar_new(); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
  if (app.state.exports.empty()) {
    GtkWidget* status = adw_status_page_new(); adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "folder-download-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(status), "No Downloads Yet"); adw_status_page_set_description(ADW_STATUS_PAGE(status), "Files you export from Figma will appear here.");
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), status);
  } else {
    GtkWidget* scroll = gtk_scrolled_window_new(); GtkWidget* clamp = adw_clamp_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget* group = adw_preferences_group_new();
    gtk_widget_set_margin_start(group, 18); gtk_widget_set_margin_end(group, 18); gtk_widget_set_margin_top(group, 18); gtk_widget_set_margin_bottom(group, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp), group);
    for (const auto& e : app.state.exports) {
      GtkWidget* row = adw_action_row_new(); char* basename = g_path_get_basename(e.path.c_str());
      adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), basename); g_free(basename);
      adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), false);
      const std::string subtitle = e.title + " · " + e.date;
      adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
      adw_action_row_add_prefix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("text-x-generic-symbolic"));
      g_object_set_data_full(G_OBJECT(row), "path", g_strdup(e.path.c_str()), g_free);
      GtkWidget* open = icon_button("external-link-symbolic", "Open File", nullptr);
      GtkWidget* reveal = icon_button("folder-symbolic", "Show in Files", nullptr);
      gtk_widget_set_valign(open, GTK_ALIGN_CENTER); gtk_widget_set_valign(reveal, GTK_ALIGN_CENTER);
      adw_action_row_add_suffix(ADW_ACTION_ROW(row), reveal); adw_action_row_add_suffix(ADW_ACTION_ROW(row), open);
      const bool exists = g_file_test(e.path.c_str(), G_FILE_TEST_EXISTS);
      gtk_widget_set_sensitive(open, exists); gtk_widget_set_sensitive(reveal, exists);
      g_object_set_data(G_OBJECT(open), "parent-window", window); g_object_set_data(G_OBJECT(reveal), "parent-window", window);
      g_signal_connect(open, "clicked", G_CALLBACK(+[](GtkButton* button, GtkWidget* row) {
        launch_file(GTK_WINDOW(g_object_get_data(G_OBJECT(button), "parent-window")), static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path")), false);
      }), row);
      g_signal_connect(reveal, "clicked", G_CALLBACK(+[](GtkButton* button, GtkWidget* row) {
        launch_file(GTK_WINDOW(g_object_get_data(G_OBJECT(button), "parent-window")), static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path")), true);
      }), row);
      adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    }
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);
  }
  adw_dialog_set_child(dialog, toolbar); adw_dialog_present(dialog, GTK_WIDGET(window));
}
void Window::show_about() {
  AdwDialog* dialog = adw_about_dialog_new();
  adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(dialog), "Select for Figma"); adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(dialog), app_id);
  adw_about_dialog_set_version(ADW_ABOUT_DIALOG(dialog), APP_VERSION);
  adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(dialog), "A Figma workspace for GNOME, inspired by ReSketch.");
  adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0_ONLY);
  adw_about_dialog_add_link(ADW_ABOUT_DIALOG(dialog), "Original ReSketch", "https://github.com/Ralsei-x64/ReSketch");
  const std::string debug = std::string("GTK ") + std::to_string(gtk_get_major_version()) + "." + std::to_string(gtk_get_minor_version()) +
      "\nlibadwaita " + std::to_string(adw_get_major_version()) + "." + std::to_string(adw_get_minor_version()) +
      "\nCEF " CEF_VERSION "\nRenderer: " + (app.software ? "compatibility (CPU frame upload)" : "GPU shared frames");
  adw_about_dialog_set_debug_info(ADW_ABOUT_DIALOG(dialog), debug.c_str());
  adw_dialog_present(dialog, GTK_WIDGET(window));
}
void Window::show_shortcuts() {
  if (auto* reference = static_cast<GWeakRef*>(g_object_get_data(G_OBJECT(window), "shortcuts-window"))) {
    if (auto* existing = G_OBJECT(g_weak_ref_get(reference))) {
      gtk_window_present(GTK_WINDOW(existing)); g_object_unref(existing); return;
    }
  }
  // Use the compact, paged GTK layout requested from Nautilus. It remains
  // supported in GTK 4; the newer Adwaita dialog has a different row layout.
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  GtkWindow* shortcuts = GTK_WINDOW(g_object_new(GTK_TYPE_SHORTCUTS_WINDOW,
    "title", "Shortcuts", "transient-for", window, "modal", true, "destroy-with-parent", true, nullptr));
  auto* page = GTK_SHORTCUTS_SECTION(g_object_new(GTK_TYPE_SHORTCUTS_SECTION,
    "section-name", "main", "title", "Shortcuts", "max-height", 12, nullptr));
  auto group = [page, this](const char* title, std::initializer_list<std::pair<const char*, const char*>> items) {
    auto* section = GTK_SHORTCUTS_GROUP(g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", title, nullptr));
    for (const auto& [label, action] : items) {
      char** keys = gtk_application_get_accels_for_action(GTK_APPLICATION(app.gtk_app), action);
      auto* item = GTK_SHORTCUTS_SHORTCUT(g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT,
        "title", label, "accelerator", keys[0] ? keys[0] : "", nullptr));
      gtk_shortcuts_group_add_shortcut(section, item); g_strfreev(keys);
    }
    gtk_shortcuts_section_add_group(page, section);
  };
  group("Tabs and Windows", {{"New Tab", "win.new-tab"}, {"Open Figma Link", "win.open-link"},
    {"New Window", "win.new-window"}, {"Close Tab", "win.close-tab"}, {"Close Window", "win.close-window"},
    {"Tab Overview", "win.overview"}});
  auto* tab_group = GTK_SHORTCUTS_GROUP(g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", "Switch Tabs", nullptr));
  for (const auto& [title, accelerator] : {std::pair{"Cycle Tabs", "<Primary>Tab"}, {"Cycle Tabs Backward", "<Primary><Shift>Tab"}, {"Next Tab", "<Primary>Page_Down"}, {"Previous Tab", "<Primary>Page_Up"}})
    gtk_shortcuts_group_add_shortcut(tab_group, GTK_SHORTCUTS_SHORTCUT(g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT,
      "title", title, "accelerator", accelerator, nullptr)));
  gtk_shortcuts_section_add_group(page, tab_group);
  group("Navigation", {{"Home", "win.home"}, {"Back", "win.back"}, {"Forward", "win.forward"},
    {"Reload", "win.reload"}, {"Reload Without Cache", "win.hard-reload"}});
  group("General", {{"Copy File Link", "win.copy-link"}, {"Downloads", "win.downloads"},
    {"Preferences", "win.preferences"}, {"Keyboard Shortcuts", "win.shortcuts"}, {"Fullscreen", "win.fullscreen"}});
  gtk_shortcuts_window_add_section(GTK_SHORTCUTS_WINDOW(shortcuts), page);
  G_GNUC_END_IGNORE_DEPRECATIONS
  auto* reference = new GWeakRef;
  g_weak_ref_init(reference, shortcuts);
  g_object_set_data_full(G_OBJECT(window), "shortcuts-window", reference, +[](gpointer p) {
    auto* reference = static_cast<GWeakRef*>(p); g_weak_ref_clear(reference); delete reference;
  });
  g_object_set_data_full(G_OBJECT(shortcuts), "capture", gtk_widget_paintable_new(GTK_WIDGET(shortcuts)), g_object_unref);
  gtk_window_present(shortcuts);
}

void Application::take_screenshot(Window& window, const std::string& filename) {
  take_screenshot(window.window, filename);
}
void Application::take_screenshot(GtkWindow* window, const std::string& filename) {
  GtkWidget* widget = GTK_WIDGET(window);
  auto* capture = GDK_PAINTABLE(g_object_get_data(G_OBJECT(window), "capture"));
  if (!capture) return;
  GtkSnapshot* snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(capture, GDK_SNAPSHOT(snapshot), gtk_widget_get_width(widget), gtk_widget_get_height(widget));
  GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
  if (node) {
    GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
    graphene_rect_t bounds; gsk_render_node_get_bounds(node, &bounds);
    GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &bounds);
    if (texture) { gdk_texture_save_to_png(texture, filename.c_str()); g_object_unref(texture); }
    gsk_render_node_unref(node);
  }
}
