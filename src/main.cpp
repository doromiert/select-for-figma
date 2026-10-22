#include "application.hpp"
#include "fonts.hpp"
#include "clipboard.hpp"
#include "build-info.hpp"
#include "policy.hpp"
#include "workspace-tabs.hpp"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <mutex>
#include <unistd.h>
#define Window X11Window
#include <gdk/x11/gdkx.h>
#undef Window

namespace {
GtkWidget* preference_row(GtkWidget* widget, const char* title) {
  if (ADW_IS_PREFERENCES_ROW(widget) && !g_strcmp0(adw_preferences_row_get_title(ADW_PREFERENCES_ROW(widget)), title)) return widget;
  for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) if (auto* found = preference_row(child, title)) return found;
  return nullptr;
}
bool contains_label(GtkWidget* widget,const char* text) {
  if (GTK_IS_LABEL(widget) && !g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)),text)) return true;
  for (auto* child = gtk_widget_get_first_child(widget);child;child = gtk_widget_get_next_sibling(child)) if (contains_label(child,text)) return true;
  return false;
}
GtkWidget* menu_action(GtkWidget* widget, const char* name) {
  if (GTK_IS_ACTIONABLE(widget) && g_strcmp0(gtk_actionable_get_action_name(GTK_ACTIONABLE(widget)), name) == 0) return widget;
  if (GTK_IS_LABEL(widget) && g_strcmp0(name, "win.tab-group-new") == 0 && g_str_has_prefix(gtk_label_get_text(GTK_LABEL(widget)), "New Group")) {
    for (auto* parent = gtk_widget_get_parent(widget); parent; parent = gtk_widget_get_parent(parent))
      if (g_strcmp0(G_OBJECT_TYPE_NAME(parent), "GtkModelButton") == 0) return parent;
  }
  for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
    if (auto* result = menu_action(child, name)) return result;
  return nullptr;
}
class PermissionProbe final : public CefPermissionPromptCallback {
public:
  cef_permission_request_result_t result = CEF_PERMISSION_RESULT_IGNORE;
  void Continue(cef_permission_request_result_t value) override { result = value; }
private:
  IMPLEMENT_REFCOUNTING(PermissionProbe);
};
class Engine final : public CefApp, public CefBrowserProcessHandler {
public:
  bool diagnostic = false;
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return Clipboard::renderer_handler(); }
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnBeforeCommandLineProcessing(const CefString& process, CefRefPtr<CefCommandLine> command) override {
    if (process.empty()) {
      if (diagnostic) command->AppendSwitchWithValue("password-store", "basic");
      command->AppendSwitch("enable-gpu");
      // OnBeforePopup applies our origin policy; OAuth must retain its opener.
      command->AppendSwitch("disable-popup-blocking");
      command->AppendSwitchWithValue("ozone-platform", g_getenv("WAYLAND_DISPLAY") ? "wayland" : "x11");
      command->AppendSwitchWithValue("use-angle", "gl-egl");
    }
  }
  void OnScheduleMessagePumpWork(int64_t delay) override {
    std::lock_guard<std::mutex> lock(mutex_);
    delay = std::clamp<int64_t>(delay, 0, 33);
    const gint64 due = g_get_monotonic_time() + delay * 1000;
    if (source_ && due >= due_) return;
    if (source_) g_source_remove(source_);
    due_ = due;
    source_ = g_timeout_add_full(G_PRIORITY_DEFAULT, static_cast<guint>(delay), +[](gpointer p) -> gboolean {
      auto* self = static_cast<Engine*>(p);
      { std::lock_guard<std::mutex> lock(self->mutex_); self->source_ = 0; }
      CefDoMessageLoopWork();
      // CEF's official external-pump sample requires this fallback to service IPC.
      self->OnScheduleMessagePumpWork(33);
      return G_SOURCE_REMOVE;
    }, this, nullptr);
  }
  void stop() { std::lock_guard<std::mutex> lock(mutex_); if (source_) { g_source_remove(source_); source_ = 0; } }
private:
  std::mutex mutex_;
  guint source_ = 0;
  gint64 due_ = 0;
  IMPLEMENT_REFCOUNTING(Engine);
};
}

int main(int argc, char** argv) {
  CefMainArgs args(argc, argv);
  CefRefPtr<Engine> engine = new Engine();
  const int subprocess = CefExecuteProcess(args, engine, nullptr);
  if (subprocess >= 0) return subprocess;

  gboolean software = false, self_test = false, render_test = false, auth_test = false;
  char* screenshot = nullptr;
  const GOptionEntry options[] = {
    {"software-rendering", 0, 0, G_OPTION_ARG_NONE, &software, "Use CPU frame upload for graphics compatibility", nullptr},
    {"self-test", 0, 0, G_OPTION_ARG_NONE, &self_test, "Exercise native UI and exit", nullptr},
    {"render-test", 0, 0, G_OPTION_ARG_NONE, &render_test, "Render an offline WebGL diagnostic and exit", nullptr},
    {"auth-test", 0, 0, G_OPTION_ARG_NONE, &auth_test, "Exercise sign-in popup handoff against a local fixture and exit", nullptr},
    {"screenshot", 0, 0, G_OPTION_ARG_FILENAME, &screenshot, "Save a screenshot during a diagnostic", "FILE"},
    {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
  };
  GOptionContext* context = g_option_context_new("[FIGMA-LINK]"); g_option_context_add_main_entries(context, options, nullptr);
  GError* error = nullptr;
  if (!g_option_context_parse(context, &argc, &argv, &error)) { g_printerr("%s\n", error->message); g_error_free(error); return 1; }
  g_option_context_free(context);
  if (!self_test && !render_test && !auth_test) {
    // Older processes must release their Chromium profile before it moves.
    if (auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr)) {
      auto* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", "io.github.doromiert.FigmaGtk"), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
      gboolean running = false;
      if (reply) { g_variant_get(reply, "(b)", &running); g_variant_unref(reply); }
      g_object_unref(bus);
      if (running) { g_printerr("Close the older Select for Figma instance before launching this version.\n"); return 1; }
    }
    if (!State::migrate_legacy_directory(g_get_user_data_dir())) { g_printerr("Could not migrate saved Select for Figma data.\n"); return 1; }
  }
  Application app;
  app.software = software; app.smoke_test = self_test; app.render_test = render_test;
  app.auth_test = auth_test;
  engine->diagnostic = self_test || render_test || auth_test;
  if (screenshot) { app.screenshot = screenshot; g_free(screenshot); }
  if (self_test || render_test || auth_test) {
    g_application_set_application_id(G_APPLICATION(app.gtk_app), "io.github.doromiert.SelectForFigma.Diagnostic");
    char* temporary = g_strdup("/tmp/select-for-figma-diagnostic-XXXXXX");
    if (!g_mkdtemp(temporary)) { g_free(temporary); return 1; }
    app.state = State(temporary); g_free(temporary);
    app.state.local_fonts = false;
    if (g_getenv("SELECT_FOR_FIGMA_TEST_NARROW")) app.state.width = 480;
    g_print("Diagnostic data: %s\n", app.state.directory().c_str());
  }

  if (!g_application_register(G_APPLICATION(app.gtk_app), nullptr, &error)) { g_printerr("%s\n", error->message); g_error_free(error); return 1; }
  if (g_application_get_is_remote(G_APPLICATION(app.gtk_app))) {
    auto* build = g_action_group_get_action_state(G_ACTION_GROUP(app.gtk_app),"build-id");
    const bool same = build && g_variant_is_of_type(build,G_VARIANT_TYPE_STRING) && !std::strcmp(g_variant_get_string(build,nullptr),APP_BUILD_ID);
    if (build) g_variant_unref(build);
    if (!same) {
      g_printerr("An older Select for Figma process is still running. The updated build was not launched.\n");
      auto* bus = g_application_get_dbus_connection(G_APPLICATION(app.gtk_app));
      auto* result = bus ? g_dbus_connection_call_sync(bus,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixProcessID",g_variant_new("(s)",g_application_get_application_id(G_APPLICATION(app.gtk_app))),G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NONE,3000,nullptr,nullptr) : nullptr;
      if (result) { guint pid; g_variant_get(result,"(u)",&pid); g_variant_unref(result); g_printerr("Close that process first (PID %u). If no window remains, stop it with: kill -TERM %u\nThen run nix run . again.\n",pid,pid); }
      else g_printerr("Close Select completely, including any background process, then launch again.\n");
      return 1;
    }
    return app.run(argc,argv);
  }
  app.apply_appearance();
  GtkIconTheme* icons = gtk_icon_theme_get_for_display(gdk_display_get_default());
  std::string icon_path = std::filesystem::current_path().string() + "/data";
  gtk_icon_theme_add_search_path(icons, icon_path.c_str());

  CefSettings settings;
  g_mkdir_with_parents(app.state.directory().c_str(), 0700);
  settings.external_message_pump = true;
  settings.windowless_rendering_enabled = true;
  settings.persist_session_cookies = true;
  CefString(&settings.root_cache_path) = app.state.cache_directory();
  CefString(&settings.cache_path) = app.state.cache_directory() + "/profile";
  CefString(&settings.log_file) = app.state.directory() + "/chromium.log";
  settings.log_severity = LOGSEVERITY_WARNING;
  std::string resource_dir = CEF_RESOURCE_DIR;
  char executable[4096]; const auto length = readlink("/proc/self/exe", executable, sizeof executable - 1);
  if (length > 0) {
    executable[length] = 0;
    const auto installed = std::filesystem::path(executable).parent_path();
    if (std::filesystem::exists(installed / "icudtl.dat")) resource_dir = installed.string();
  }
  CefString(&settings.resources_dir_path) = resource_dir;
  CefString(&settings.locales_dir_path) = resource_dir + "/locales";
  // GOptionContext rewrites argv; CEF must receive the updated argument count.
  CefMainArgs browser_args(argc, argv);
  if (!CefInitialize(browser_args, settings, engine, nullptr)) { g_printerr("Could not initialize Chromium. See %s/chromium.log\n", app.state.directory().c_str()); return 1; }
  app.cef_ready = true;
  if (app.state.local_fonts) app.fonts = std::make_unique<Fonts>();

  if (self_test || render_test || auth_test) {
    app.state.restore_session = false;
    app.state.recent.clear();
    Window* window = app.new_window();
    if (auth_test) {
      const char* fixture = g_getenv("SELECT_FOR_FIGMA_AUTH_TEST_URL");
      if (!fixture || std::string(fixture).rfind("http://127.0.0.1:", 0) != 0) {
        g_printerr("Auth diagnostic requires a local SELECT_FOR_FIGMA_AUTH_TEST_URL\n");
        app.diagnostic_failed = true; window->close();
      } else {
        window->current()->navigate(fixture);
        if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACCOUNT")) {
          auto* account=app.zane_account_window(false);
          if (account) { std::string url=fixture; url.replace(url.find("role=main"),9,"role=zane"); account->current()->navigate(url); }
          gtk_window_present(window->window);
          g_timeout_add(5500,+[](gpointer p)->gboolean {
            auto* window=static_cast<Window*>(p); auto browser=window->current();
            if (browser && browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript("console.info('select-zane-account-isolation:'+(document.cookie.includes('presence_role=main')?'PASS':'FAIL'))", "select-for-figma://diagnostic",1);
            return G_SOURCE_REMOVE;
          },window);
        }
        if (g_getenv("SELECT_FOR_FIGMA_TEST_NOTIFICATIONS")) {
          GNotification* notification = g_notification_new("Native notification diagnostic");
          g_notification_set_body(notification, "Private test bus only");
          g_application_send_notification(G_APPLICATION(app.gtk_app), "diagnostic", notification); g_object_unref(notification);
        }
      }
    }
    if (self_test) {
      char** keys = gtk_application_get_accels_for_action(GTK_APPLICATION(app.gtk_app), "win.new-tab");
      guint key = 0; GdkModifierType modifiers{};
      const bool bound = keys[0] && gtk_accelerator_parse(keys[0], &key, &modifiers) && key == GDK_KEY_t && modifiers == GDK_CONTROL_MASK;
      g_strfreev(keys);
      g_action_group_activate_action(G_ACTION_GROUP(window->window), "new-tab", nullptr);
      g_action_group_activate_action(G_ACTION_GROUP(window->window), "new-tab", nullptr);
      const bool new_tabs = bound && adw_tab_view_get_n_pages(window->tabs) == 2 && window->current() && Policy::is_new_tab(window->current()->url) && !window->current()->is_home;
      g_print("Ctrl+T diagnostic: %s\n", new_tabs ? "PASS" : "FAIL");
      if (!new_tabs) app.diagnostic_failed = true;
      while (adw_tab_view_get_n_pages(window->tabs)) adw_tab_view_close_page(window->tabs, adw_tab_view_get_nth_page(window->tabs, 0));
      window->native_home();
      for (const char* name : {"First document", "Second document with a long title", "Third document"}) {
        auto page = window->open(std::string("https://www.figma.com/design/") + std::to_string(adw_tab_view_get_n_pages(window->tabs)), false, true);
        page->name = name;
      }
      auto* doc = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
      CefRefPtr<PermissionProbe> probe = new PermissionProbe;
      app.state.local_fonts = true;
      doc->OnShowPermissionPrompt(nullptr, 1, "https://www.figma.com", CEF_PERMISSION_TYPE_LOOPBACK_NETWORK, probe);
      bool permissions = probe->result == CEF_PERMISSION_RESULT_ACCEPT;
      doc->OnShowPermissionPrompt(nullptr, 2, "https://www.figma.com", CEF_PERMISSION_TYPE_LOCAL_NETWORK, probe);
      permissions &= probe->result == CEF_PERMISSION_RESULT_DENY;
      doc->OnShowPermissionPrompt(nullptr, 3, "https://accounts.google.com", CEF_PERMISSION_TYPE_LOOPBACK_NETWORK, probe);
      permissions &= probe->result == CEF_PERMISSION_RESULT_DENY;
      app.state.local_fonts = false;
      doc->OnShowPermissionPrompt(nullptr, 4, "https://www.figma.com", CEF_PERMISSION_TYPE_LOOPBACK_NETWORK, probe);
      permissions &= probe->result == CEF_PERMISSION_RESULT_DENY;
      doc->OnShowPermissionPrompt(nullptr, 5, "https://www.figma.com", CEF_PERMISSION_TYPE_NOTIFICATIONS, probe);
      permissions &= probe->result == CEF_PERMISSION_RESULT_ACCEPT;
      app.state.notifications = false;
      doc->OnShowPermissionPrompt(nullptr, 6, "https://www.figma.com", CEF_PERMISSION_TYPE_NOTIFICATIONS, probe);
      permissions &= probe->result == CEF_PERMISSION_RESULT_DENY;
      app.state.notifications = true;
      g_print("Permissions diagnostic: %s\n", permissions ? "PASS" : "FAIL");
      if (!permissions) app.diagnostic_failed = true;
      auto* header_tabs = GTK_WIDGET(g_object_get_data(G_OBJECT(window->window), "header-tabs"));
      window->open("https://www.figma.com/design/0");
      const auto* selected = adw_tab_view_get_selected_page(window->tabs);
      bool selection = window->current() && !window->current()->is_home && !gtk_widget_has_css_class(header_tabs, "home-active");
      window->native_home();
      selection &= window->current() && window->current()->is_home && gtk_widget_has_css_class(header_tabs, "home-active") && adw_tab_view_get_selected_page(window->tabs) == selected;
      g_print("Home selection diagnostic: %s\n", selection ? "PASS" : "FAIL");
      if (!selection) app.diagnostic_failed = true;
      g_timeout_add(100, +[](gpointer p) -> gboolean { static_cast<Window*>(p)->update(); return G_SOURCE_REMOVE; }, window);
    }
    if (render_test) {
      if (g_getenv("SELECT_FOR_FIGMA_TEST_DRAWER_LAYOUT")) gtk_window_set_default_size(window->window,1900,1000);
      std::string html = R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>Chromium Render Check</title><style>body{font:18px system-ui;margin:0;padding:48px;background:#fafafa;color:#222}canvas{width:320px;height:240px;border-radius:12px}input{font:inherit;padding:12px;margin:24px 0}p{max-width:600px;line-height:1.5}</style></head><body><h1>Chromium rendering check</h1><p id="status">Checking WebGL…</p><canvas id="canvas" width="320" height="240"></canvas><br><input placeholder="Input method check"><script>const c=document.querySelector('canvas'), gl=c.getContext('webgl2')||c.getContext('webgl');document.querySelector('#status').textContent=gl?'WebGL is available. This green canvas is rendered by Chromium.':'WebGL unavailable';if(gl){gl.clearColor(.18,.72,.40,1);gl.clear(gl.COLOR_BUFFER_BIT)}document.title=gl?'WebGL OK':'WebGL FAILED';</script></body></html>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_EDITOR_THEME")) {
        char* fixture = nullptr; gsize size;
        if (!g_file_get_contents("tools/editor-theme-fixture.html", &fixture, &size, nullptr)) return 1;
        html.assign(fixture, size); g_free(fixture);
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_KEYBOARD_INPUT")) html += R"HTML(
        <textarea id="keyboard-text" style="position:fixed;left:40px;top:100px;width:400px;height:150px">Keep selected text</textarea>
        <input id="keyboard-after" style="position:fixed;left:40px;top:290px;width:400px">
        <div id="keyboard-editable" contenteditable="true" style="position:fixed;left:40px;top:400px;width:400px;height:100px"></div>
        <script>
          const typing=document.querySelector('#keyboard-text'), editable=document.querySelector('#keyboard-editable'), after=document.querySelector('#keyboard-after');
          let keyboardStage=0, downs=0, presses=0, ups=0, beforeInputs=0, inputs=0, compositions=0, keyboardFailed=false;
          const resetCounts=()=>{downs=presses=ups=beforeInputs=inputs=compositions=0;};
          const prepare=(node,text)=>{
            if(node.isContentEditable) node.textContent=text; else node.value=text;
            node.focus();
            if(node.isContentEditable){const range=document.createRange();range.selectNodeContents(node);getSelection().removeAllRanges();getSelection().addRange(range);}else node.select();
            resetCounts();
          };
          typing.onclick=()=>{if(keyboardStage===0)typing.select();};
          document.addEventListener('keydown',event=>{downs++;if([0,2,11].includes(keyboardStage)&&event.key==='t')event.preventDefault();});
          document.addEventListener('keypress',()=>presses++);
          document.addEventListener('beforeinput',()=>beforeInputs++);
          document.addEventListener('input',()=>inputs++);
          document.addEventListener('compositionstart',()=>compositions++);
          document.addEventListener('keyup',event=>{
            const keys=['t','a','t','b','C','Enter','Backspace','Tab','Enter','Enter','a','t','d'];
            if(event.key!==keys[keyboardStage])return;
            ups++;
            setTimeout(()=>{
              const evidence={stage:keyboardStage,text:typing.value,editable:editable.textContent,after:after.value,downs,presses,ups,beforeInputs,inputs,compositions};
              let ok=false,label='';
              switch(keyboardStage++){
                case 0:label='Prevented T preserves selected textarea text';ok=typing.value==='Keep selected text'&&inputs===0&&beforeInputs===0&&downs===1&&ups===1&&presses===0;prepare(typing,'');break;
                case 1:label='Textarea typing emits one key and insertion sequence';ok=typing.value==='a'&&downs===1&&presses===1&&ups===1&&beforeInputs===1&&inputs===1;prepare(editable,'Keep canvas text');break;
                case 2:label='Prevented T preserves selected contenteditable text';ok=editable.textContent==='Keep canvas text'&&inputs===0&&beforeInputs===0&&presses===0;prepare(editable,'');break;
                case 3:label='Contenteditable typing inserts once';ok=editable.textContent==='b'&&presses===1&&beforeInputs===1&&inputs===1;resetCounts();break;
                case 4:label='Shift produces one uppercase character';ok=editable.textContent==='bC'&&presses===1&&beforeInputs===1&&inputs===1;prepare(typing,'');break;
                case 5:label='Enter inserts one newline';ok=typing.value==='\n'&&inputs===1;resetCounts();break;
                case 6:label='Backspace deletes once';ok=typing.value===''&&inputs===1;resetCounts();break;
                case 7:label='Tab moves focus without inserting text';ok=document.activeElement===after&&typing.value===''&&inputs===0;prepare(after,'');break;
                case 8:label='GTK Unicode composition commits once';ok=after.value==='é'&&compositions>0;prepare(after,'');break;
                case 9:label='GTK supplementary Unicode composition commits once';ok=after.value==='😀'&&compositions>0;resetCounts();break;
                case 10:label='Ctrl A selects text without insertion';ok=after.value==='😀'&&after.selectionStart===0&&after.selectionEnd===2&&inputs===0;resetCounts();break;
                case 11:label='Prevented T still works after composition';ok=after.value==='😀'&&inputs===0&&presses===0;resetCounts();break;
                case 12:label='Typing after composition replaces selection once';ok=after.value==='d'&&presses===1&&beforeInputs===1&&inputs===1;break;
              }
              keyboardFailed ||= !ok;
              console.info('select-input-check:'+label+':'+(ok?'PASS':'FAIL'));
              if(!ok)console.info('Keyboard evidence: '+JSON.stringify(evidence));
              if(keyboardStage===13)document.title=keyboardFailed?'Keyboard FAILED':'WebGL + Keyboard OK';
            },30);
          });
        </script>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_COPY")) html += R"HTML(<span id="copy-selection">Selected page text ✓</span><input id="copy-input"><button style="position:fixed;left:600px;top:300px;width:120px;height:60px" onclick="const el=document.querySelector('#copy-input');el.focus();el.select();document.execCommand('copy')">Copy fixture</button><script>
        window.copyMode='';
        document.addEventListener('copy',e=>{if(copyMode==='rich'){e.clipboardData.setData('text/plain','Figma objects ✓');e.clipboardData.setData('text/html','<b>Figma objects ✓</b>');e.clipboardData.setData('application/x-figma','fixture-objects');e.preventDefault();e.stopImmediatePropagation();}});
        window.copyStage=async stage=>{
          copyMode=''; const el=document.querySelector('#copy-input');el.focus();
          if(stage===0){el.value='Selected input text ✓';el.select();}
          if(stage===1){el.blur();const range=document.createRange();range.selectNodeContents(document.querySelector('#copy-selection'));getSelection().removeAllRanges();getSelection().addRange(range);}
          if(stage===2) await navigator.clipboard.writeText('Copy button text ✓');
          if(stage===3){el.value='Legacy copy button ✓';el.select();}
          if(stage===4){copyMode='rich';el.select();}
          if(stage===5){el.value='keepCUTtail';el.setSelectionRange(4,7);}
          if(stage===6){const png=await new Promise(resolve=>document.querySelector('canvas').toBlob(resolve));const blobs={'text/plain':new Blob(['Async rich clipboard ✓'],{type:'text/plain'}),'text/html':new Blob(['<i>Async rich clipboard ✓</i>'],{type:'text/html'}),'image/png':png};await navigator.clipboard.write([{types:Object.keys(blobs),getType:async type=>blobs[type]}]);}
        };
        </script>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE")) html += R"HTML(<div data-testid="comment-message">Alice: Please fix the alignment.</div><div data-testid="comment-message">Bob: I agree with Alice.</div><script>document.addEventListener('keydown',e=>{if(e.key==='/'&&!window.nativeSpeechFixture){let input=document.createElement('input');input.dataset.testid='cursor-chat-input';input.placeholder='Cursor chat';document.body.append(input);input.focus();}});</script>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_SITE_HOOKS")) html += R"HTML(<script>console.info=()=>{};document.addEventListener('keydown',e=>e.stopImmediatePropagation(),true);document.addEventListener('input',e=>e.stopImmediatePropagation(),true);</script>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE")) {
        char* fixture = nullptr; gsize size;
        if (g_file_get_contents("tools/native-cursor-fixture.js", &fixture, &size, nullptr)) {
          std::string source(fixture,size);g_free(fixture);
          if(g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CANVAS_SCRIPT")) {const std::string marker="select-reference-make-node";const auto at=source.find(marker);if(at!=std::string::npos) source.replace(at,marker.size(),"removed-transaction-binding");}
          html += "<script>"+source+"</script>";
        }
        else { g_printerr("Native cursor fixture missing\n"); app.diagnostic_failed = true; }
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_NATIVE")) html += R"HTML(<textarea id="canvas-carrier" aria-hidden="true" style="position:fixed;left:-100px;top:-100px;width:1px;height:1px;opacity:0"></textarea><script>window.nativeSpeechFixture=true;document.querySelector('#canvas-carrier').addEventListener('beforeinput',e=>e.preventDefault());</script>)HTML";
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACTIONS")) html += R"HTML(<script>
        window.fixtureCanvasInput={down:false,up:false,moves:0,wheel:false,key:false};
        document.addEventListener('mousedown',e=>{if(e.isTrusted&&e.button===1)fixtureCanvasInput.down=true;});
        document.addEventListener('mouseup',e=>{if(e.isTrusted&&e.button===1)fixtureCanvasInput.up=true;});
        document.addEventListener('mousemove',e=>{if(e.isTrusted&&(e.buttons&4))fixtureCanvasInput.moves++;});
        document.addEventListener('wheel',e=>{if(e.isTrusted&&e.ctrlKey&&e.deltaY<0)fixtureCanvasInput.wheel=true;e.preventDefault();},{passive:false});
        document.addEventListener('keydown',e=>{if(e.isTrusted&&e.shiftKey&&e.code==='Digit1')fixtureCanvasInput.key=true;});
      </script>)HTML";
      char* encoded = g_base64_encode(reinterpret_cast<const guchar*>(html.data()), html.size());
      const std::string address = std::string("data:text/html;base64,") + encoded;
      auto first = window->open(address); g_free(encoded);
      if (g_getenv("SELECT_FOR_FIGMA_TEST_TAB_SWITCHER")) g_timeout_add_full(G_PRIORITY_DEFAULT, 1500, +[](gpointer p) -> gboolean {
        auto first = *static_cast<CefRefPtr<Browser>*>(p); auto* window = first->owner();
        auto second = window->open(first->url+"#second", false, true);
        auto third = window->open(first->url+"#third", false, true);
        first->name = "First document OK"; second->name = "Second document OK"; third->name = "Third document OK";
        const auto group = window->workspace->create_group("Grouped tabs", third);
        window->workspace->toggle_group(group);
        window->native_home(); window->current()->navigate(first->url+"#home"); window->workspace->activate(first);
        window->app.state.tab_switcher_mru = false;
        struct SwitchTest { Window* window; CefRefPtr<Browser> first, second, third; int step = 0; };
        g_timeout_add_full(G_PRIORITY_DEFAULT, 450, +[](gpointer p) -> gboolean {
          auto* test = static_cast<SwitchTest*>(p); auto* w = test->window;
          auto* panel = GTK_WIDGET(g_object_get_data(G_OBJECT(w->window), "tab-switcher"));
          auto* grid = GTK_FLOW_BOX(g_object_get_data(G_OBJECT(w->window), "tab-switcher-grid"));
          auto* keys = GTK_EVENT_CONTROLLER_KEY(g_object_get_data(G_OBJECT(w->window), "tab-switcher-keys"));
          auto check = [w](bool ok, const char* name) { g_print("Tab switcher %s diagnostic: %s\n", name, ok ? "PASS" : "FAIL"); if (!ok) w->app.diagnostic_failed = true; };
          auto press = [keys](guint key, GdkModifierType state) { gboolean handled = false; g_signal_emit_by_name(keys, "key-pressed", key, 0, state, &handled); return !!handled; };
          auto forward = [&] { return press(GDK_KEY_Tab, GDK_CONTROL_MASK); };
          auto backward = [&] { return press(GDK_KEY_ISO_Left_Tab, GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK)); };
          auto release = [keys] { g_signal_emit_by_name(keys, "key-released", GDK_KEY_Control_L, 0, GdkModifierType(0)); };
          auto selected = [grid] { auto* children = gtk_flow_box_get_selected_children(grid); const int index = children ? gtk_flow_box_child_get_index(GTK_FLOW_BOX_CHILD(children->data)) : -1; g_list_free(children); return index; };
          switch (test->step++) {
            case 0:
              gtk_widget_grab_focus(test->first->surface());
              check(forward() && gtk_widget_get_visible(panel) && selected() == 2 && w->current() == test->first && gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(keys)) == GTK_PHASE_CAPTURE, "position and deferred activation"); break;
            case 1:
              w->app.take_screenshot(*w, w->app.screenshot+".switcher.png");
              {
                auto* tile = gtk_flow_box_child_get_child(gtk_flow_box_get_child_at_index(grid, 1));
                check(!GTK_IS_IMAGE(gtk_widget_get_first_child(tile)), "cached document thumbnail");
                press(GDK_KEY_Up, GDK_CONTROL_MASK); check(selected() == 3, "grid row up");
                press(GDK_KEY_Down, GDK_CONTROL_MASK); check(selected() == 2, "grid row down");
              }
              forward(); check(selected() == 3, "group member"); backward(); check(selected() == 2, "reverse");
              release(); check(w->current() == test->second && !gtk_widget_get_visible(panel), "release commits"); break;
            case 2:
              forward(); forward(); check(selected() == 0, "Home in position order"); release(); check(w->current()->is_home, "Home activation"); break;
            case 3:
              backward(); check(selected() == 3, "Home reverse wrap"); press(GDK_KEY_Escape, GdkModifierType(0)); release(); check(w->current()->is_home && !gtk_widget_get_visible(panel), "Escape cancels");
              w->app.state.tab_switcher_mru = true;
              w->workspace->activate(test->first); w->workspace->activate(test->third); w->workspace->activate(test->second);
              forward(); check(selected() == 1, "recent order"); forward(); check(selected() == 2, "frozen recent order"); backward(); release(); check(w->current() == test->third, "recent activation"); break;
            case 4:
              forward(); release(); check(w->current() == test->second, "recent toggle back");
              forward(); release(); check(w->current() == test->third, "recent toggle forward");
              forward(); g_signal_emit_by_name(grid, "child-activated", gtk_flow_box_get_child_at_index(grid, selected())); check(w->current() == test->second && !gtk_widget_get_visible(panel), "click commits");
              w->workspace->activate(test->third); break;
            case 5:
              forward(); adw_tab_view_close_page(w->tabs, adw_tab_view_get_page(w->tabs, test->second->widget())); break;
            case 6:
              check(!gtk_widget_get_visible(panel), "closed candidate cancels");
              forward(); release(); check(w->current() == test->first, "closed candidate pruned");
              check(!press(GDK_KEY_Tab, GdkModifierType(0)), "ordinary Tab passes through");
              for (int i = 0; i < 20; ++i) { auto tab = w->open(test->first->url+"#extra"+std::to_string(i), false, true); tab->name = "Extra document "+std::to_string(i+1); }
              gtk_window_set_default_size(w->window, 480, 600); backward(); break;
            case 7:
              w->app.take_screenshot(*w, w->app.screenshot+".many.png");
              check(gtk_widget_get_height(panel) <= 450 && gtk_widget_get_width(panel) <= gtk_widget_get_width(GTK_WIDGET(w->window)) && selected() == 22, "many tabs fit narrow window");
              {
                auto* scroll = GTK_SCROLLED_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(grid), GTK_TYPE_SCROLLED_WINDOW));
                auto* adjustment = gtk_scrolled_window_get_vadjustment(scroll); graphene_rect_t bounds;
                const bool measured = gtk_widget_compute_bounds(GTK_WIDGET(gtk_flow_box_get_child_at_index(grid, selected())), GTK_WIDGET(grid), &bounds);
                check(measured && bounds.origin.y >= gtk_adjustment_get_value(adjustment)-1 && bounds.origin.y+bounds.size.height <= gtk_adjustment_get_value(adjustment)+gtk_adjustment_get_page_size(adjustment)+1, "selected thumbnail visible after wrap");
                graphene_rect_t first, second;
                check(gtk_widget_compute_bounds(GTK_WIDGET(gtk_flow_box_get_child_at_index(grid, 0)), GTK_WIDGET(grid), &first) && gtk_widget_compute_bounds(GTK_WIDGET(gtk_flow_box_get_child_at_index(grid, 1)), GTK_WIDGET(grid), &second) && second.origin.x > first.origin.x && second.origin.y == first.origin.y, "thumbnail grid has columns");
              }
              press(GDK_KEY_Escape, GdkModifierType(0)); w->show_preferences();
              if (auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(w->window))) {
                auto* row = preference_row(GTK_WIDGET(dialog), "Ctrl+Tab Order");
                check(row && ADW_IS_COMBO_ROW(row) && adw_combo_row_get_selected(ADW_COMBO_ROW(row)) == 1, "preference present");
                if (row) { adw_combo_row_set_selected(ADW_COMBO_ROW(row), 0); check(!w->app.state.tab_switcher_mru, "preference updates"); }
                adw_dialog_close(dialog);
              } else check(false, "preference dialog");
              g_object_set_data(G_OBJECT(w->window), "tab-switcher-tested", GINT_TO_POINTER(1)); return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        }, new SwitchTest{window, first, second, third}, +[](gpointer p) { delete static_cast<SwitchTest*>(p); });
        return G_SOURCE_REMOVE;
      }, new CefRefPtr<Browser>(first), +[](gpointer p) { delete static_cast<CefRefPtr<Browser>*>(p); });
      if (g_getenv("SELECT_FOR_FIGMA_TEST_KEYBOARD_INPUT")) g_timeout_add_full(G_PRIORITY_DEFAULT,1500,+[](gpointer p)->gboolean {
        auto browser=*static_cast<CefRefPtr<Browser>*>(p);auto* window=browser->owner();if(!window)return G_SOURCE_REMOVE;
        graphene_rect_t bounds;
        if(!gtk_widget_compute_bounds(browser->surface(),GTK_WIDGET(window->window),&bounds)){window->app.diagnostic_failed=true;return G_SOURCE_REMOVE;}
        double sx,sy;gtk_native_get_surface_transform(GTK_NATIVE(window->window),&sx,&sy);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        char* request=g_strdup_printf("{\"window\":%lu,\"click\":true,\"button\":1,\"start\":[%d,%d],\"end\":[0,0],\"shortcuts\":[[\"t\"],[\"a\"],[\"t\"],[\"b\"],[\"Shift_L\",\"c\"],[\"Return\"],[\"BackSpace\"],[\"Tab\"],[\"Control_L\",\"Shift_L\",\"u\"],[\"e\"],[\"9\"],[\"Return\"],[\"Control_L\",\"Shift_L\",\"u\"],[\"1\"],[\"f\"],[\"6\"],[\"0\"],[\"0\"],[\"Return\"],[\"Control_L\",\"a\"],[\"t\"],[\"d\"]]}",gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))),static_cast<int>(bounds.origin.x+100+sx),static_cast<int>(bounds.origin.y+150+sy));
        G_GNUC_END_IGNORE_DEPRECATIONS
        g_file_set_contents((window->app.screenshot+".drag.json").c_str(),request,-1,nullptr);g_free(request);
        return G_SOURCE_REMOVE;
      },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      if (g_getenv("SELECT_FOR_FIGMA_TEST_EDITOR_THEME")) g_timeout_add_full(G_PRIORITY_DEFAULT, 2000, +[](gpointer p) -> gboolean {
        struct ThemeTest { CefRefPtr<Browser> browser; int step = 0; };
        g_timeout_add_full(G_PRIORITY_DEFAULT, 500, +[](gpointer p) -> gboolean {
          auto* test = static_cast<ThemeTest*>(p); auto browser = test->browser; auto* window = browser->owner();
          if (!window || !browser->cef_browser()) return G_SOURCE_REMOVE;
          auto frame = browser->cef_browser()->GetMainFrame();
          // Exercise the real CEF :hover state, including Figma's Share style.
          if (test->step == 0 || test->step == 4) {
            CefMouseEvent hover; hover.x = gtk_widget_get_width(browser->surface())-48; hover.y = 40;
            browser->cef_browser()->GetHost()->SendMouseMoveEvent(hover, false);
          }
          switch (test->step++) {
            case 0:
              window->app.state.appearance = "light"; window->app.apply_appearance(); break;
            case 1:
              frame->ExecuteJavaScript("checkTheme('light')", "select-for-figma://diagnostic", 1);
              window->app.take_screenshot(*window, window->app.screenshot+".light.png");
              window->app.state.appearance = "dark"; window->app.apply_appearance(); break;
            case 2:
              frame->ExecuteJavaScript("checkTheme('dark'); if(themeResults.light.bg===themeResults.dark.bg) console.info('select-editor-theme:palette-change:FAIL'); else console.info('select-editor-theme:palette-change:PASS');", "select-for-figma://diagnostic", 1);
              window->app.take_screenshot(*window, window->app.screenshot+".dark.png");
              window->show_preferences(); break;
            case 3: {
              auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window));
              auto* toggle = dialog ? preference_row(GTK_WIDGET(dialog), "Adwaita Editor Theme") : nullptr;
              const bool available = toggle && ADW_IS_SWITCH_ROW(toggle) && adw_switch_row_get_active(ADW_SWITCH_ROW(toggle));
              g_print("Editor theme preference diagnostic: %s\n", available ? "PASS" : "FAIL");
              if (!available) { window->app.diagnostic_failed = true; return G_SOURCE_REMOVE; }
              adw_switch_row_set_active(ADW_SWITCH_ROW(toggle), false); adw_dialog_close(dialog); break;
            }
            case 4:
              frame->ExecuteJavaScript("checkTheme('disabled')", "select-for-figma://diagnostic", 1);
              window->app.take_screenshot(*window, window->app.screenshot+".disabled.png");
              window->app.state.adwaita_editor = true; browser->refresh_theme(); break;
            case 5:
              frame->ExecuteJavaScript("checkTheme('restored'); homeFixture(true)", "select-for-figma://diagnostic", 1);
              browser->url = "https://www.figma.com/files/recent"; browser->refresh_theme(); break;
            case 6:
              frame->ExecuteJavaScript("checkHomeTheme()", "select-for-figma://diagnostic", 1);
              window->app.take_screenshot(*window, window->app.screenshot+".homepage.png");
              browser->url = "https://www.figma.com/login"; browser->refresh_theme(); break;
            case 7:
              frame->ExecuteJavaScript("checkTheme('disabled'); homeFixture(false)", "select-for-figma://diagnostic", 1);
              browser->url = frame->GetURL(); browser->refresh_theme(); return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        }, new ThemeTest{*static_cast<CefRefPtr<Browser>*>(p)}, +[](gpointer p) { delete static_cast<ThemeTest*>(p); });
        return G_SOURCE_REMOVE;
      }, new CefRefPtr<Browser>(first), +[](gpointer p) { delete static_cast<CefRefPtr<Browser>*>(p); });
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACTIONS")) g_timeout_add_full(G_PRIORITY_DEFAULT,6800,+[](gpointer p)->gboolean {
        auto browser=*static_cast<CefRefPtr<Browser>*>(p);
        if(browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(R"JS((()=>{
          const input=window.fixtureCanvasInput,nodes=Object.values(window.fixtureNodes),paths=nodes.filter(node=>node.type==='VECTOR'&&node.fixtureNetwork);
          const inputs=input.down&&input.up&&input.moves>0&&input.wheel&&input.key;
          const copies=paths.length===3&&paths.every(node=>node.parentNode.guid!=='1:2')&&window.fixtureNodes['1:3'].characters==='Original design';
          console.info('select-zane-canvas-actions:'+JSON.stringify({input,paths:paths.length,copies})+':'+(inputs&&copies?'PASS':'FAIL'));
        })())JS","select-for-figma://diagnostic",1);
        return G_SOURCE_REMOVE;
      },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CURSOR")) g_timeout_add_full(G_PRIORITY_DEFAULT,4200,+[](gpointer p) -> gboolean {
        auto browser = *static_cast<CefRefPtr<Browser>*>(p);
        if (browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(R"JS((() => {
          const host=document.querySelector('#select-zane-native'), cursor=host?.firstElementChild;
          const name=host?.querySelector('[class*="multiplayer_cursors--nameContainer--"]');
          const visible=!!host&&!host.hidden&&!!name&&name.getBoundingClientRect().width>0;
          const before=cursor?.getBoundingClientRect().x;
          window.fixturePanX=40;
          requestAnimationFrame(()=>requestAnimationFrame(()=>{
            const after=cursor?.getBoundingClientRect().x;
            console.info('select-native-cursor-pan:'+(visible&&Math.abs(before-after-40)<2?'PASS':'FAIL'));
            if (!window.followupFixture) {
              window.__selectZane.present({visible:true,busy:false,x:5000,y:-1000,space:'canvas',text:name.innerText.replace(/^Zane\s*/, '')});
              setTimeout(()=>{
                const offscreen=cursor.getBoundingClientRect().x>innerWidth;
                document.querySelector('#select-zane-avatar').click();
                setTimeout(()=>{
                  const followed=Math.abs(window.fixturePanX-5000)<1&&Math.abs(window.fixturePanY+1000)<1;
                  const clean=!host.querySelector('button');
                  console.info('select-native-cursor-follow:'+(offscreen&&followed&&clean?'PASS':'FAIL'));
                  window.__selectZane.present({visible:true,busy:false,x:5000,y:-1000,space:'canvas',follow_user:true,text:name.innerText.replace(/^Zane\s*/, '')});
                  document.dispatchEvent(new PointerEvent('pointermove',{clientX:400,clientY:300,bubbles:true}));
                  setTimeout(()=>{
                    const before=cursor.getBoundingClientRect().x;
                    window.fixturePanX+=100;
                    requestAnimationFrame(()=>requestAnimationFrame(()=>{
                      console.info('select-native-cursor-user-follow:'+(Math.abs(cursor.getBoundingClientRect().x-before)<2?'PASS':'FAIL'));
                    }));
                  },100);
                },650);
              },550);
            }
          }));
        })())JS", "select-for-figma://diagnostic", 1);
        return G_SOURCE_REMOVE;
      },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_FOLLOWUP")) {
        g_timeout_add_full(G_PRIORITY_DEFAULT,3500,+[](gpointer p)->gboolean {
          auto browser=*static_cast<CefRefPtr<Browser>*>(p);
          if (browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(R"JS((() => {
            window.followupFixture=true;
            document.dispatchEvent(new PointerEvent('pointermove',{clientX:100,clientY:120,bubbles:true}));
            window.__selectZane.nativeKey('Escape'); document.activeElement?.blur();
            window.__selectZane.nativeKey('/'); window.__selectZane.commit('also read the comments');
            window.__selectZane.nativeKey('Enter');
          })())JS","select-for-figma://diagnostic",1);
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        g_timeout_add_full(G_PRIORITY_DEFAULT,5500,+[](gpointer p)->gboolean {
          auto browser=*static_cast<CefRefPtr<Browser>*>(p);
          if (browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(R"JS((() => {
            document.dispatchEvent(new PointerEvent('pointermove',{clientX:180,clientY:200,bubbles:true}));
            window.__selectZane.nativeKey('Escape'); document.activeElement?.blur();
            window.__selectZane.nativeKey('/'); window.__selectZane.commit("yo you're free to go"); window.__selectZane.nativeKey('Enter');
            setTimeout(()=>{
              const hidden=document.querySelector('#select-zane-native')?.hidden&&document.querySelector('#select-zane-avatar')?.hidden;
              window.__selectZane.nativeKey('/'); window.__selectZane.commit('read these comments too'); window.__selectZane.nativeKey('Enter');
              console.info('select-zane-followup:'+(hidden?'PASS':'FAIL'));
            },300);
          })())JS","select-for-figma://diagnostic",1);
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        g_timeout_add_full(G_PRIORITY_DEFAULT,6300,+[](gpointer p)->gboolean {
          auto browser=*static_cast<CefRefPtr<Browser>*>(p);
          if (browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript("window.__selectZane.nativeKey('Escape'); window.__selectZane.nativeKey('/'); window.__selectZane.commit('hey zane read the comments'); window.__selectZane.nativeKey('Enter');","select-for-figma://diagnostic",1);
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_MODELS")) g_timeout_add(400, +[](gpointer p) -> gboolean {
        auto* window = static_cast<Window*>(p); window->show_preferences();
        g_timeout_add(80, +[](gpointer p) -> gboolean {
          auto* window = static_cast<Window*>(p); if (window->destroyed) return G_SOURCE_REMOVE;
          auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window));
          if (!dialog) return G_SOURCE_REMOVE;
          auto* model = ADW_COMBO_ROW(preference_row(GTK_WIDGET(dialog), "Chat Model"));
          auto* effort = ADW_COMBO_ROW(preference_row(GTK_WIDGET(dialog), "Chat Reasoning"));
          auto* work = ADW_COMBO_ROW(preference_row(GTK_WIDGET(dialog), "Work Model"));
          auto* reasoning = ADW_COMBO_ROW(preference_row(GTK_WIDGET(dialog), "Work Reasoning"));
          if (!model || !effort || !work || !reasoning || !gtk_widget_get_sensitive(GTK_WIDGET(model)) || !gtk_widget_get_sensitive(GTK_WIDGET(work))) return G_SOURCE_CONTINUE;
          bool valid = g_list_model_get_n_items(adw_combo_row_get_model(model)) == 3;
          adw_combo_row_set_selected(model, 1); valid &= g_list_model_get_n_items(adw_combo_row_get_model(effort)) == 2;
          adw_combo_row_set_selected(effort, 1);
          adw_combo_row_set_selected(model, 2); valid &= window->app.state.zane_chat_effort.empty() && g_list_model_get_n_items(adw_combo_row_get_model(effort)) == 3;
          adw_combo_row_set_selected(model, 1); adw_combo_row_set_selected(effort, 1);
          adw_combo_row_set_selected(work, 2); adw_combo_row_set_selected(reasoning, 1);
          State saved(window->app.state.directory()); valid &= saved.zane_chat_model == "fixture-fast" && saved.zane_chat_effort == "low" && saved.zane_work_model == "fixture-deep" && saved.zane_work_effort == "high";
          g_print("Zane model and effort selectors diagnostic: %s\n", valid ? "PASS" : "FAIL");
          if (!valid) window->app.diagnostic_failed = true;
          g_timeout_add(400, +[](gpointer p) -> gboolean {
            auto* window = static_cast<Window*>(p); if (window->destroyed) return G_SOURCE_REMOVE;
            window->app.take_screenshot(*window, window->app.screenshot + ".models.png");
            if (auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window))) adw_dialog_close(dialog);
            return G_SOURCE_REMOVE;
          }, window);
          return G_SOURCE_REMOVE;
        }, window); return G_SOURCE_REMOVE;
      }, window);
      if (g_getenv("SELECT_FOR_FIGMA_TEST_CREATION_RACE")) {
        // Reproduce navigation/reload before asynchronous OnAfterCreated.
        std::string stale = html; const auto color = stale.find(".18,.72,.40"); stale.replace(color,11,".20,.35,.85");
        char* data = g_base64_encode(reinterpret_cast<const guchar*>(stale.data()),stale.size());
        first->start(); first->navigate(std::string("data:text/html;base64,")+data); first->reload(); first->navigate(address); g_free(data);
      }

      if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_STATE")) g_timeout_add_full(G_PRIORITY_DEFAULT,100,+[](gpointer p) -> gboolean {
        auto b = *static_cast<CefRefPtr<Browser>*>(p); if (b->closing) return G_SOURCE_REMOVE;
        if (b->cef_browser()) { CefMouseEvent event; event.x = 64; event.y = 220; b->cef_browser()->GetHost()->SendMouseMoveEvent(event,false); b->cef_browser()->GetHost()->Invalidate(PET_VIEW); }
        return G_SOURCE_CONTINUE;
      },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      if (g_getenv("SELECT_FOR_FIGMA_TEST_COPY")) g_timeout_add_full(G_PRIORITY_DEFAULT,1800,+[](gpointer p) -> gboolean {
        Clipboard::check_copy(**static_cast<CefRefPtr<Browser>*>(p)); return G_SOURCE_REMOVE;
      },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_NATIVE")) {
        g_timeout_add_full(G_PRIORITY_DEFAULT,1800,+[](gpointer p) -> gboolean {
          auto b = *static_cast<CefRefPtr<Browser>*>(p);
          if (b->cef_browser()) b->cef_browser()->GetMainFrame()->ExecuteJavaScript("document.querySelector('input').focus(); window.__selectZane.nativeKey('/'); window.__selectZane.commit('hey zane form text');","select-for-figma://diagnostic",1);
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        g_timeout_add_full(G_PRIORITY_DEFAULT,2600,+[](gpointer p) -> gboolean {
          auto b = *static_cast<CefRefPtr<Browser>*>(p);
          if (!b->cef_browser()) return G_SOURCE_REMOVE;
          auto* bubble = GTK_WIDGET(g_object_get_data(G_OBJECT(b->widget()),"zane-bubble"));
          const bool quiet = bubble && !gtk_widget_get_visible(bubble);
          g_print("Form text does not wake Zane diagnostic: %s\n",quiet ? "PASS" : "FAIL"); if (!quiet && b->owner()) b->owner()->app.diagnostic_failed = true;
          b->OnAddressChange(b->cef_browser(),b->cef_browser()->GetMainFrame(),"https://www.figma.com/design/fixture");
          b->cef_browser()->GetMainFrame()->ExecuteJavaScript("document.querySelector('#canvas-carrier').focus();","select-for-figma://diagnostic",1);
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        g_timeout_add_full(G_PRIORITY_DEFAULT,3000,+[](gpointer p) -> gboolean {
          auto b = *static_cast<CefRefPtr<Browser>*>(p); gtk_widget_grab_focus(b->surface());
          if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_KEYBOARD")) {
            auto* window = b->owner(); graphene_rect_t bounds{};
            if (!window || !gtk_widget_compute_bounds(b->surface(), GTK_WIDGET(window->window), &bounds)) return G_SOURCE_REMOVE;
            double sx, sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window), &sx, &sy);
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            char* request = g_strdup_printf("{\"window\":%lu,\"click\":true,\"button\":1,\"start\":[%d,%d],\"end\":[0,0],\"keys\":\"/hey zane read the comments\\n\",\"dwell\":0.8}", gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))), static_cast<int>(bounds.origin.x+100+sx), static_cast<int>(bounds.origin.y+220+sy));
            G_GNUC_END_IGNORE_DEPRECATIONS
            g_file_set_contents((window->app.screenshot+".drag.json").c_str(), request, -1, nullptr); g_free(request);
            return G_SOURCE_REMOVE;
          }
          auto* controllers = gtk_widget_observe_controllers(b->surface());
          for (unsigned i=0;i<g_list_model_get_n_items(controllers);++i) {
            auto* controller = g_list_model_get_item(controllers,i);
            if (GTK_IS_EVENT_CONTROLLER_KEY(controller)) { gboolean handled; g_signal_emit_by_name(controller,"key-pressed",GDK_KEY_slash,61,GdkModifierType(0),&handled); }
            g_object_unref(controller);
          }
          g_object_unref(controllers);
          auto* im = GTK_IM_CONTEXT(g_object_get_data(G_OBJECT(b->surface()),"browser-im")); if (im) g_signal_emit_by_name(im,"commit","hey zane read the comments");
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE") && !g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_NATIVE")) {
        g_timeout_add_full(G_PRIORITY_DEFAULT,1800,+[](gpointer p) -> gboolean {
          auto browser = *static_cast<CefRefPtr<Browser>*>(p);
          if (browser->cef_browser()) {
            std::string wake="document.activeElement?.blur(); document.dispatchEvent(new KeyboardEvent('keydown',{key:'/',bubbles:true})); const chat=document.activeElement; chat.value='hey zane read the comments'; chat.dispatchEvent(new Event('input',{bubbles:true}));";
            if(g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACTIONS")) wake+="window.__selectZane.nativeKey('Enter');document.activeElement?.blur();";
            browser->cef_browser()->GetMainFrame()->ExecuteJavaScript(wake,"select-for-figma://diagnostic",1);
          }
          return G_SOURCE_REMOVE;
        },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CANCEL") || g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_RECOVER")) {
          g_timeout_add_full(G_PRIORITY_DEFAULT,3600,+[](gpointer p) -> gboolean {
            auto browser = *static_cast<CefRefPtr<Browser>*>(p); auto* window = browser->owner(); if (!window || window->destroyed) return G_SOURCE_REMOVE;
            auto* bubble = GTK_WIDGET(g_object_get_data(G_OBJECT(browser->widget()),"zane-bubble"));
            auto* stop = bubble ? GTK_WIDGET(g_object_get_data(G_OBJECT(bubble),"zane-stop")) : nullptr;
            auto* entry = bubble ? GTK_WIDGET(g_object_get_data(G_OBJECT(bubble),"zane-entry")) : nullptr;
            auto* reply = bubble ? GTK_WIDGET(g_object_get_data(G_OBJECT(bubble),"zane-reply")) : nullptr;
            bool passed = entry && reply;
            if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CANCEL")) { passed &= stop && gtk_widget_get_visible(stop); if (stop) g_signal_emit_by_name(stop,"clicked"); passed &= reply && std::string(gtk_label_get_text(GTK_LABEL(reply))) == "Stopped."; }
            else passed &= reply && g_str_has_prefix(gtk_label_get_text(GTK_LABEL(reply)),"Sign in to Codex");
            g_print("Zane cancellation/recovery diagnostic: %s\n",passed ? "PASS" : "FAIL"); if (!passed) window->app.diagnostic_failed = true;
            if (entry) { gtk_editable_set_text(GTK_EDITABLE(entry),"read the comments"); g_signal_emit_by_name(entry,"activate"); }
            return G_SOURCE_REMOVE;
          },new CefRefPtr<Browser>(first),+[](gpointer p){delete static_cast<CefRefPtr<Browser>*>(p);});
        }
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_OVERVIEW") || g_getenv("SELECT_FOR_FIGMA_TEST_TAB_SLIDE")) {
        std::string second_html = html;
        auto color = second_html.find(".18,.72,.40"); second_html.replace(color, 11, ".20,.35,.85");
        char* content = g_base64_encode(reinterpret_cast<const guchar*>(second_html.data()), second_html.size());
        window->open(std::string("data:text/html;base64,")+content); g_free(content);
        auto* return_to = new CefRefPtr<Browser>(first);
        g_timeout_add_full(G_PRIORITY_DEFAULT, 2500, +[](gpointer p) -> gboolean {
          auto browser = *static_cast<CefRefPtr<Browser>*>(p); if (auto* window = browser->owner()) window->workspace->activate(browser); return G_SOURCE_REMOVE;
        }, return_to, +[](gpointer p) { delete static_cast<CefRefPtr<Browser>*>(p); });
      }
      if (g_getenv("SELECT_FOR_FIGMA_TEST_TAB_SLIDE")) g_timeout_add(3500, +[](gpointer p) -> gboolean {
        struct SlideTest { Window* window; int step = 0; };
        g_timeout_add_full(G_PRIORITY_DEFAULT, 100, +[](gpointer p) -> gboolean {
          auto* test = static_cast<SlideTest*>(p); auto* window = test->window; auto* ui = window->workspace.get();
          auto* first = adw_tab_view_get_nth_page(window->tabs, 0); auto* second = adw_tab_view_get_nth_page(window->tabs, 1);
          const auto check = [&](bool passed, const char* name) {
            g_print("Tab slide %s diagnostic: %s; offset=%.3f\n", name, passed ? "PASS" : "FAIL", ui->page_transition_offset());
            if (!passed) window->app.diagnostic_failed = true;
          };
          switch (test->step++) {
            case 0: adw_tab_view_set_selected_page(window->tabs, second); break;
            case 1:
              check(ui->page_transition_offset() > 0 && ui->page_transition_offset() < 1, "right");
              window->app.take_screenshot(*window, window->app.screenshot+".right.png");
              adw_tab_view_set_selected_page(window->tabs, first); break;
            case 2:
              check(ui->page_transition_offset() < 0 && ui->page_transition_offset() > -1, "left");
              window->app.take_screenshot(*window, window->app.screenshot+".left.png");
              adw_tab_view_set_selected_page(window->tabs, second); break;
            case 3:
              check(ui->page_transition_offset() > 0 && ui->page_transition_offset() < 1, "interrupted");
              window->app.take_screenshot(*window, window->app.screenshot+".interrupted.png"); break;
            case 6: {
              check(ui->page_transition_offset() == 0, "settled");
              auto* settings = gtk_widget_get_settings(GTK_WIDGET(window->window));
              g_object_set(settings, "gtk-enable-animations", false, nullptr);
              adw_tab_view_set_selected_page(window->tabs, first);
              check(ui->page_transition_offset() == 0, "reduced motion");
              g_object_set(settings, "gtk-enable-animations", true, nullptr);
              ui->show_overview(true); break;
            }
            case 11:
              ui->activate(static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(second)), "browser"))); break;
            case 17:
              check(adw_tab_view_get_selected_page(window->tabs) == second && ui->page_transition_offset() == 0 && ui->transition_progress() == 0, "overview uses zoom");
              window->native_home();
              window->current()->navigate(static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(first)), "browser"))->url);
              break;
            case 18:
              check(window->current()->is_home && ui->page_transition_offset() < 0 && ui->page_transition_offset() > -1, "to Home");
              window->app.take_screenshot(*window, window->app.screenshot+".to-home.png"); break;
            case 21:
              check(window->current()->is_home && ui->page_transition_offset() == 0, "Home settled");
              ui->activate(static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(first)), "browser"))); break;
            case 22:
              check(!window->current()->is_home && ui->page_transition_offset() > 0 && ui->page_transition_offset() < 1, "from Home");
              window->app.take_screenshot(*window, window->app.screenshot+".from-home.png");
              window->native_home(); break;
            case 23:
              check(window->current()->is_home && ui->page_transition_offset() < 0 && ui->page_transition_offset() > -1, "Home interrupted"); break;
            case 26: {
              auto* settings = gtk_widget_get_settings(GTK_WIDGET(window->window));
              g_object_set(settings, "gtk-enable-animations", false, nullptr);
              ui->activate(static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(first)), "browser")));
              check(!window->current()->is_home && ui->page_transition_offset() == 0, "Home reduced motion");
              g_object_set(settings, "gtk-enable-animations", true, nullptr);
              return G_SOURCE_REMOVE;
            }
          }
          return G_SOURCE_CONTINUE;
        }, new SlideTest{static_cast<Window*>(p)}, +[](gpointer p) { delete static_cast<SlideTest*>(p); });
        return G_SOURCE_REMOVE;
      }, window);
    }
    g_timeout_add_seconds(render_test && g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_SCRIPT") ? 20 : render_test && g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_USER_INPUT") ? 15 : render_test && g_getenv("SELECT_FOR_FIGMA_TEST_COPY") ? 12 : (render_test || auth_test) ? 8 : 2, +[](gpointer p) -> gboolean {
      auto* app = static_cast<Application*>(p); Window* window = app->active_window();
      if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACCOUNT")) for (auto& candidate : app->windows) {
        if (!candidate->zane_account && !candidate->destroyed) { window=candidate.get(); break; }
      }
      if (window) {
        auto* header = GTK_WIDGET(g_object_get_data(G_OBJECT(window->window), "main-header"));
        g_print("Header height: %d logical pixels\n", gtk_widget_get_height(header));
        if (app->smoke_test || app->render_test) {
          auto* tabs = GTK_WIDGET(g_object_get_data(G_OBJECT(window->window), "header-tabs"));
          auto* reload = GTK_WIDGET(g_object_get_data(G_OBJECT(window->window), "header-reload"));
          graphene_rect_t tab_bounds, reload_bounds;
          if (gtk_widget_get_width(header) <= 600) {
            g_print("Narrow header: %d px; tabs in overview\n", gtk_widget_get_width(header));
            if (gtk_widget_get_mapped(tabs) || gtk_widget_get_height(header) > 48) app->diagnostic_failed = true;
          } else if (gtk_widget_compute_bounds(tabs, header, &tab_bounds) && gtk_widget_compute_bounds(reload, header, &reload_bounds)) {
            g_print("Tab strip: %.0f px wide; gap after Reload: %.0f px\n", tab_bounds.size.width,
              tab_bounds.origin.x - reload_bounds.origin.x - reload_bounds.size.width);
            if (tab_bounds.size.width < gtk_widget_get_width(header) / 2 || gtk_widget_get_height(header) > 48) app->diagnostic_failed = true;
          } else app->diagnostic_failed = true;
        }
        if (!app->screenshot.empty()) app->take_screenshot(*window, app->screenshot);
        if (app->auth_test) {
          auto browser = window->current();
          bool passed = browser && !browser->is_auth_popup && browser->name == "AUTH PASS" && adw_tab_view_get_n_pages(window->tabs) == 0;
          if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACCOUNT")) {
            passed=browser&&browser->cef_browser()&&app->zane_account_context&&g_object_get_data(G_OBJECT(browser->widget()),"zane-account-isolation-ok")&&
              !browser->cef_browser()->GetHost()->GetRequestContext()->IsSame(app->zane_account_context);
            for (auto& candidate : app->windows) if (candidate->zane_account) {
              const bool hidden=!gtk_widget_get_mapped(GTK_WIDGET(candidate->window));
              g_print("Account session stays in background diagnostic: %s\n",hidden ? "PASS" : "FAIL"); passed &= hidden;
              auto peer=candidate->current();auto* frame=peer ? peer->account_frame() : nullptr;
              const bool pixels=frame&&gdk_texture_get_width(frame)>=1100&&gdk_texture_get_height(frame)>=750;
              g_print("Background account canvas capture diagnostic: %s\n",pixels ? "PASS" : "FAIL");passed &= pixels;
              if(frame) g_object_unref(frame);
            }
            g_print("Separate account browser context diagnostic: %s\n",passed ? "PASS" : "FAIL");
          }
          g_print("Authentication handoff: %s; document tabs=%d\n", passed ? "PASS" : "FAIL", adw_tab_view_get_n_pages(window->tabs));
          if (!passed) {
            g_print("Local fixture title: %s; current popup=%s\n", browser ? browser->name.c_str() : "missing", browser && browser->is_auth_popup ? "yes" : "no");
            if (browser && browser->cef_browser()) browser->cef_browser()->GetMainFrame()->ExecuteJavaScript("console.log('ready: '+document.readyState+' hidden: '+document.hidden+' focus: '+document.hasFocus());", "select-for-figma://diagnostic", 1);
          }
          if (!passed) app->diagnostic_failed = true;
        } else if (app->render_test) {
          auto browser = window->current();
          g_print("Render diagnostic: title=%s; GPU shared frames=%s\n", browser ? browser->name.c_str() : "missing", browser && browser->gpu_frames ? "yes" : "no");
          if (g_getenv("SELECT_FOR_FIGMA_TEST_KEYBOARD_INPUT")) {
            const int count=browser?GPOINTER_TO_INT(g_object_get_data(G_OBJECT(browser->widget()),"input-checks")):0;
            g_print("Editor input checks completed diagnostic: %s; %d/13\n",count==13?"PASS":"FAIL",count);
            if(count!=13 || browser->name!="WebGL + Keyboard OK")app->diagnostic_failed=true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_TAB_SWITCHER")) {
            const bool complete = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window->window), "tab-switcher-tested"));
            g_print("Tab switcher completed diagnostic: %s\n", complete ? "PASS" : "FAIL"); if (!complete) app->diagnostic_failed = true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_EDITOR_THEME")) {
            const bool complete = browser && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(browser->widget()), "editor-theme-checks")) == 7;
            g_print("Editor theme checks completed diagnostic: %s\n", complete ? "PASS" : "FAIL");
            if (!complete) app->diagnostic_failed = true;
          }
          if (!browser || browser->name.find("OK") == std::string::npos) app->diagnostic_failed = true;
          if (g_getenv("SELECT_FOR_FIGMA_TEST_CREATION_RACE")) {
            const bool one = Browser::active_count == 1;
            g_print("Single browser after creation/navigation race diagnostic: %s; active=%u\n",one ? "PASS" : "FAIL",Browser::active_count);
            if (!one) app->diagnostic_failed = true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_STATE")) {
            auto* paintable = gtk_widget_paintable_new(browser->surface()); auto* snapshot = gtk_snapshot_new();
            gdk_paintable_snapshot(paintable,snapshot,gtk_widget_get_width(browser->surface()),gtk_widget_get_height(browser->surface())); g_object_unref(paintable);
            auto* node = gtk_snapshot_free_to_node(snapshot); bool painted = false;
            if (node) {
              auto* texture = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window->window)),node,nullptr);
              const int w = gdk_texture_get_width(texture), h = gdk_texture_get_height(texture);
              std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4); gdk_texture_download(texture,pixels.data(),w*4);
              const int x = 64*w/gtk_widget_get_width(browser->surface()), y = 220*h/gtk_widget_get_height(browser->surface());
              if (x < w && y < h) { const auto* p = pixels.data()+(y*w+x)*4; painted = p[1] > 140 && p[2] < 100 && p[0] < 140 && p[3] == 255; }
              g_object_unref(texture); gsk_render_node_unref(node);
            }
            g_print("Editor redraw GL state diagnostic: %s\n",painted ? "PASS" : "FAIL"); if (!painted) app->diagnostic_failed = true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_COPY")) {
            const bool copied = browser && g_object_get_data(G_OBJECT(browser->widget()),"copy-check-pass");
            g_print("Native copy clipboard diagnostic: %s\n",copied ? "PASS" : "FAIL"); if (!copied) app->diagnostic_failed = true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE")) {
            if(g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_SCRIPT")&&!g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CANVAS_SCRIPT")) {
              char* report=nullptr;gsize length=0;const auto path=app->state.directory()+"/zane-last-error.json";
              const bool recorded=g_file_get_contents(path.c_str(),&report,&length,nullptr)&&std::string(report,length).find("unsupported key: NoSuchKey")!=std::string::npos;
              g_free(report);g_print("Zane precise failure record diagnostic: %s\n",recorded?"PASS":"FAIL");if(!recorded)app->diagnostic_failed=true;
            }
            auto* bubble = browser ? GTK_WIDGET(g_object_get_data(G_OBJECT(browser->widget()),"zane-bubble")) : nullptr;
            auto* reply = bubble ? GTK_WIDGET(g_object_get_data(G_OBJECT(bubble),"zane-reply")) : nullptr;
            const bool native = browser && g_object_get_data(G_OBJECT(browser->widget()), "zane-native-cursor");
            const bool awake = native || (bubble && gtk_widget_get_visible(bubble) && gtk_widget_get_mapped(bubble));
            auto* cursor = bubble ? gtk_widget_get_first_child(bubble) : nullptr;
            const bool visible_cursor = native || (cursor && GTK_IS_DRAWING_AREA(cursor) && gtk_widget_get_mapped(cursor) && gtk_widget_get_width(cursor) >= 28 && gtk_widget_get_height(cursor) >= 36);
            if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_CURSOR")) {
              const bool replaced = native && bubble && !gtk_widget_get_visible(bubble) && g_object_get_data(G_OBJECT(browser->widget()), "native-cursor-pan-ok") &&
                g_object_get_data(G_OBJECT(browser->widget()),g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_FOLLOWUP") ? "zane-followup-ok" : "native-cursor-follow-ok");
              if (!g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_FOLLOWUP") && !g_object_get_data(G_OBJECT(browser->widget()),"native-cursor-user-follow-ok")) app->diagnostic_failed=true;
              g_print("Native multiplayer cursor replaces GTK cursor diagnostic: %s\n", replaced ? "PASS" : "FAIL"); if (!replaced) app->diagnostic_failed = true;
            }
            const auto* full=browser?static_cast<const char*>(g_object_get_data(G_OBJECT(browser->widget()),"zane-response")):nullptr;
            const bool answered=full&&g_str_has_prefix(full,"Zane read two comments.");
            if(g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_ACTIONS")&&!g_object_get_data(G_OBJECT(browser->widget()),"zane-canvas-actions-ok")) app->diagnostic_failed=true;
            if (g_getenv("SELECT_FOR_FIGMA_TEST_ZANE_SPEECH")) {
              const char* text=reply ? gtk_label_get_text(GTK_LABEL(reply)) : "";
              const bool sequenced=g_utf8_validate(text,-1,nullptr)&&g_utf8_strlen(text,-1)>60&&g_str_has_prefix(text,"Done ✅")&&g_str_has_suffix(text,"every word.");
              g_print("Zane timed model-selected speech diagnostic: %s\n",sequenced ? "PASS" : "FAIL");if(!sequenced) app->diagnostic_failed=true;
            }
            g_print("Cursor chat wakes Zane diagnostic: %s\n",awake ? "PASS" : "FAIL");
            g_print("Local assistant cursor visible diagnostic: %s\n",visible_cursor ? "PASS" : "FAIL");
            g_print("Codex conversation and comment tools diagnostic: %s\n",answered ? "PASS" : "FAIL");
            if (!awake || !visible_cursor || !answered) { app->diagnostic_failed = true; if (reply) g_print("Zane fixture reply: %s\n",gtk_label_get_text(GTK_LABEL(reply))); }
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_CURSOR")) {
            const bool cursor = browser && browser->custom_cursor_seen;
            g_print("Custom cursor diagnostic: %s\n", cursor ? "PASS" : "FAIL");
            if (!cursor) app->diagnostic_failed = true;
          }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_OVERVIEW")) {
            auto* source = gtk_widget_get_ancestor(header, ADW_TYPE_TAB_OVERVIEW);
            auto* capture = gtk_widget_paintable_new(source); auto* frame = gdk_paintable_get_current_image(capture);
            auto* snapshot = gtk_snapshot_new(); gdk_paintable_snapshot(frame, snapshot, gtk_widget_get_width(source), gtk_widget_get_height(source));
            auto* node = gtk_snapshot_free_to_node(snapshot); bool opaque = false;
            if (node) {
              auto* renderer = gtk_native_get_renderer(GTK_NATIVE(window->window)); auto* texture = gsk_renderer_render_texture(renderer, node, nullptr);
              const int width = gdk_texture_get_width(texture), height = gdk_texture_get_height(texture);
              std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4); gdk_texture_download(texture, pixels.data(), width*4);
              opaque = height > 3 && pixels[(3*width+width/2)*4+3] == 255;
              g_object_unref(texture); gsk_render_node_unref(node);
            }
            g_object_unref(frame); g_object_unref(capture);
            g_print("Workspace chrome background diagnostic: %s\n", opaque ? "PASS" : "FAIL"); if (!opaque) app->diagnostic_failed = true;
            if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_GROUPS")) {
              auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
              auto* second = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 1))), "browser"));
              const auto id = window->workspace->create_group("Project Metro", first); if (!g_getenv("SELECT_FOR_FIGMA_TEST_DRAWER_LAYOUT")) window->workspace->assign(second, id);
            }
            if (!g_getenv("SELECT_FOR_FIGMA_TEST_HEADER_DRAG")) window->workspace->show_overview(true);
            g_timeout_add(160, +[](gpointer p) -> gboolean {
              auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) app->take_screenshot(*window, app->screenshot+".transition.png"); return G_SOURCE_REMOVE;
            }, app);
            g_timeout_add(650, +[](gpointer p) -> gboolean {
              auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) {
                app->take_screenshot(*window, app->screenshot+".overview.png");
                auto* second = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 1))), "browser"));
                if (g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_GROUPS")) {
                  auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
                  auto* card = window->workspace->overview_group(first->group_id); auto* content = gtk_button_get_child(GTK_BUTTON(card));
                  auto* grid = gtk_widget_get_first_child(gtk_widget_get_first_child(content)); int count = 0; for (auto* child = gtk_widget_get_first_child(grid); child; child = gtk_widget_get_next_sibling(child)) ++count;
                  const bool previews = GTK_IS_GRID(grid) && count == 4;
                  g_print("Group preview grid diagnostic: %s\n", previews ? "PASS" : "FAIL"); if (!previews) app->diagnostic_failed = true;
                  struct DrawerProbe { Application* app; CefRefPtr<Browser> member; double top = -1; int height = 0; };
                  auto* probe = new DrawerProbe{app,first};
                  g_signal_emit_by_name(card, "clicked");
                  if (g_getenv("SELECT_FOR_FIGMA_TEST_DRAWER_LAYOUT")) g_timeout_add(80,+[](gpointer p) -> gboolean {
                    auto* probe = static_cast<DrawerProbe*>(p); auto* app = probe->app; if (auto* window = app->active_window()) {
                      auto* ui = window->workspace.get(); auto* drawer = ui->group_drawer();
                      auto* section = gtk_widget_get_first_child(drawer); auto* heading = gtk_widget_get_first_child(gtk_widget_get_last_child(section));
                      graphene_rect_t bounds{}, header{};
                      const bool top = gtk_widget_compute_bounds(drawer,ui->overview(),&bounds) && gtk_widget_compute_bounds(heading,drawer,&header) && header.origin.y >= 0 && header.origin.y+header.size.height < bounds.size.height;
                      g_print("Drawer reveals heading first diagnostic: %s\n",top ? "PASS" : "FAIL"); if (!top) app->diagnostic_failed = true;
                      probe->top = bounds.origin.y; probe->height = bounds.size.height;
                      app->take_screenshot(*window,app->screenshot+".drawer-opening.png");
                      probe->member->name += " updated"; window->update();
                    } return G_SOURCE_REMOVE;
                  },probe);
                  g_timeout_add_full(G_PRIORITY_DEFAULT,800,+[](gpointer p) -> gboolean {
                    auto* probe = static_cast<DrawerProbe*>(p); auto* app = probe->app; if (auto* window = app->active_window()) {
                      auto* ui = window->workspace.get(); auto* member = ui->preview_button(probe->member.get());
                      const bool drawer = member && gtk_widget_get_mapped(member);
                      g_print("Group drawer diagnostic: %s\n",drawer ? "PASS" : "FAIL"); if (!drawer) app->diagnostic_failed = true;
                      if (g_getenv("SELECT_FOR_FIGMA_TEST_DRAWER_LAYOUT")) {
                        graphene_rect_t panel{}, card{};
                        const bool measured = gtk_widget_compute_bounds(ui->group_drawer(),ui->overview(),&panel) && gtk_widget_compute_bounds(ui->overview_group(probe->member->group_id),ui->overview(),&card);
                        const double gap = panel.origin.y-card.origin.y-card.size.height;
                        const bool adjacent = measured && gap >= 0 && gap <= 6;
                        const bool anchored = measured && std::abs(panel.origin.y-probe->top) <= 1 && probe->height > 0 && probe->height < panel.size.height;
                        g_print("Drawer directly below group diagnostic: %s; gap=%.0f px\n",adjacent ? "PASS" : "FAIL",gap);
                        g_print("Drawer stable reveal anchor diagnostic: %s; height=%d → %.0f\n",anchored ? "PASS" : "FAIL",probe->height,panel.size.height);
                        if (!adjacent || !anchored) app->diagnostic_failed = true;
                      }
                      app->take_screenshot(*window,app->screenshot+".drawer.png");
                    } return G_SOURCE_REMOVE;
                  },probe,+[](gpointer p) { delete static_cast<DrawerProbe*>(p); }); return G_SOURCE_REMOVE;
                }
                if (g_getenv("SELECT_FOR_FIGMA_TEST_DRAG")) {
                  auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
                  graphene_rect_t from, to;
                  auto* first_tile = g_getenv("SELECT_FOR_FIGMA_TEST_HEADER_DRAG") ? window->workspace->strip_tab(first) : window->workspace->preview_button(first);
                  auto* second_tile = g_getenv("SELECT_FOR_FIGMA_TEST_HEADER_DRAG") ? window->workspace->strip_tab(second) : window->workspace->preview_button(second);
                  const bool coordinates = gtk_widget_compute_bounds(first_tile, GTK_WIDGET(window->window), &from) && gtk_widget_compute_bounds(second_tile, GTK_WIDGET(window->window), &to);
                  if (!coordinates) { app->diagnostic_failed = true; return G_SOURCE_REMOVE; }
                  auto* surface = gtk_native_get_surface(GTK_NATIVE(window->window)); double sx, sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window), &sx, &sy);
                  g_print("Drag coordinates: from=(%.0f,%.0f) size=(%.0f,%.0f) transform=(%.0f,%.0f)\n", from.origin.x,from.origin.y,from.size.width,from.size.height,sx,sy);
                  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                  char* request = g_strdup_printf("{\"window\":%lu,\"start\":[%d,%d],\"end\":[%d,%d]}", gdk_x11_surface_get_xid(surface), static_cast<int>(from.origin.x+from.size.width/2+sx), static_cast<int>(from.origin.y+from.size.height/2+sy), static_cast<int>(to.origin.x+to.size.width*.85+sx), static_cast<int>(to.origin.y+to.size.height/2+(g_getenv("SELECT_FOR_FIGMA_TEST_HEADER_DRAG") ? 0 : 40)+sy));
                  G_GNUC_END_IGNORE_DEPRECATIONS
                  g_file_set_contents((app->screenshot+".drag.json").c_str(), request, -1, nullptr); g_free(request);
                  g_timeout_add(20, +[](gpointer p) -> gboolean {
                    auto* app = static_cast<Application*>(p);
                    if (!g_file_test((app->screenshot+".drag.done").c_str(), G_FILE_TEST_EXISTS)) return app->active_window() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
                    g_timeout_add(90, +[](gpointer p) -> gboolean {
                      auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) {
                        const auto progress = window->workspace->drop_progress(); const bool settling = progress > 0 && progress < 1;
                        g_print("Drop settle diagnostic: %s; progress=%.3f\n", settling ? "PASS" : "FAIL", progress);
                        if (!settling) app->diagnostic_failed = true;
                        app->take_screenshot(*window, app->screenshot+".settling.png");
                      } return G_SOURCE_REMOVE;
                    }, app); return G_SOURCE_REMOVE;
                  }, app);
                  g_timeout_add(1800, +[](gpointer p) -> gboolean {
                    auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) {
                      auto* last = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 1))), "browser"));
                      const bool reordered = last == window->current().get();
                      g_print("Overview drag diagnostic: %s\n", reordered ? "PASS" : "FAIL"); if (!reordered) app->diagnostic_failed = true;
                      app->take_screenshot(*window, app->screenshot+".reordered.png");
                    } return G_SOURCE_REMOVE;
                  }, app);
                  return G_SOURCE_REMOVE;
                }
                auto* image = second->thumbnail(); const bool sharp = image && gdk_paintable_get_intrinsic_width(image) >= gtk_widget_get_width(second->surface());
                g_print("Preview resolution diagnostic: %s; snapshot=%d px\n", sharp ? "PASS" : "FAIL", image ? gdk_paintable_get_intrinsic_width(image) : 0);
                if (image) g_object_unref(image);
                if (!sharp) app->diagnostic_failed = true;
                const auto before = g_get_monotonic_time(); g_signal_emit_by_name(window->workspace->preview_button(second), "clicked");
                const auto elapsed = (g_get_monotonic_time()-before)/1000;
                g_print("Overview click diagnostic: %s; handler=%lld ms\n", elapsed < 50 ? "PASS" : "FAIL", static_cast<long long>(elapsed));
                if (elapsed >= 50) app->diagnostic_failed = true;
                g_timeout_add(120, +[](gpointer p) -> gboolean {
                  auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) {
                    const auto progress = window->workspace->transition_progress(); const bool animating = progress > 0 && progress < 1;
                    g_print("Overview zoom diagnostic: %s; progress=%.3f\n", animating ? "PASS" : "FAIL", progress);
                    if (!animating) app->diagnostic_failed = true;
                    app->take_screenshot(*window, app->screenshot+".zoom-in.png");
                  } return G_SOURCE_REMOVE;
                }, app);
              } return G_SOURCE_REMOVE;
            }, app);
          }
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_GHOST_GROUPS")) {
          struct GhostProbe { Application* app; Window* window; Window* other; CefRefPtr<Browser> first, second; std::string live; int step = 0; };
          auto at = [window](int index) -> CefRefPtr<Browser> { return static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs,index))),"browser")); };
          auto first = at(0), second = at(1); auto* ui = window->workspace.get();
          ui->create_group("Ghost group",first); ui->assign(first,"");
          auto* probe = new GhostProbe{app,window,nullptr,first,second,ui->create_group("Live group",second)};
          ui->toggle_group(probe->live); ui->activate(first);
          g_timeout_add_full(G_PRIORITY_DEFAULT,600,+[](gpointer p) -> gboolean {
            auto* probe = static_cast<GhostProbe*>(p); auto* app = probe->app; auto* window = probe->window;
            switch (probe->step++) {
              case 0: {
                auto* tile = window->workspace->strip_tab(probe->first.get()); auto* controllers = gtk_widget_observe_controllers(tile);
                for (guint i = 0;i < g_list_model_get_n_items(controllers);++i) {
                  auto* controller = G_OBJECT(g_list_model_get_item(controllers,i));
                  if (GTK_IS_GESTURE_CLICK(controller) && gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(controller)) == 3) g_signal_emit_by_name(controller,"pressed",1,20.,16.);
                  g_object_unref(controller);
                }
                g_object_unref(controllers); auto* popup = gtk_widget_get_last_child(tile);
                const bool clean = GTK_IS_POPOVER(popup) && !contains_label(popup,"Ghost group") && contains_label(popup,"Live group");
                g_print("Ghost group menu diagnostic: %s\n",clean ? "PASS" : "FAIL"); if (!clean) app->diagnostic_failed = true;
                if (GTK_IS_POPOVER(popup)) gtk_popover_popdown(GTK_POPOVER(popup));
                probe->other = app->new_window(false);
                gtk_window_present(probe->other->window);
                adw_tab_view_transfer_page(window->tabs,adw_tab_view_get_page(window->tabs,probe->second->widget()),probe->other->tabs,0); app->queue_save(); break;
              }
              case 2: {
                const bool preserved = app->state.groups.size() == 1 && app->group_in_use(probe->live) && probe->second->owner() == probe->other;
                g_print("Empty group cleanup preserves other windows diagnostic: %s\n",preserved ? "PASS" : "FAIL"); if (!preserved) app->diagnostic_failed = true;
                adw_tab_view_close_page(probe->other->tabs,adw_tab_view_get_page(probe->other->tabs,probe->second->widget())); break;
              }
              case 4: {
                const bool removed = app->state.groups.empty(); g_print("Last member closes group diagnostic: %s\n",removed ? "PASS" : "FAIL"); if (!removed) app->diagnostic_failed = true;
                for (auto& item : app->windows) if (!item->closing && !item->destroyed) item->close();
                return G_SOURCE_REMOVE;
              }
            }
            return G_SOURCE_CONTINUE;
          },probe,+[](gpointer p) { delete static_cast<GhostProbe*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERFLOW")) {
          struct OverflowProbe { Application* app; Window* window; CefRefPtr<Browser> first, last; int step = 0, attempts = 0; };
          auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs,0))),"browser"));
          CefRefPtr<Browser> last;
          for (int i = adw_tab_view_get_n_pages(window->tabs);i < 35;++i) { last = window->open("https://www.figma.com/design/overflow-"+std::to_string(i),false,true); last->name = "Document "+std::to_string(i+1); }
          if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERFLOW_GROUP")) {
            const auto id = window->workspace->create_group("Many members",first);
            for (int i = 1;i < adw_tab_view_get_n_pages(window->tabs);++i) {
              auto* member = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs,i))),"browser")); window->workspace->assign(member,id);
            }
          }
          window->workspace->activate(first); auto* probe = new OverflowProbe{app,window,first,last};
          g_timeout_add_full(G_PRIORITY_DEFAULT,600,+[](gpointer p) -> gboolean {
            auto* probe = static_cast<OverflowProbe*>(p); auto* ui = probe->window->workspace.get(); auto* app = probe->app;
            auto* scroll = GTK_SCROLLED_WINDOW(g_object_get_data(G_OBJECT(ui->strip()),"scroller")); auto* adjustment = gtk_scrolled_window_get_hadjustment(scroll);
            switch (probe->step++) {
              case 0: {
                auto* first = ui->strip_tab(probe->first.get()); auto* next = GTK_WIDGET(g_object_get_data(G_OBJECT(ui->strip()),"scroll-forward"));
                const bool usable = gtk_widget_get_width(first) >= 180 && gtk_adjustment_get_upper(adjustment) > gtk_adjustment_get_page_size(adjustment) && gtk_widget_get_visible(next);
                g_print("Readable overflow tabs diagnostic: %s; width=%d\n",usable ? "PASS" : "FAIL",gtk_widget_get_width(first)); if (!usable) app->diagnostic_failed = true;
                app->take_screenshot(*probe->window,app->screenshot+".overflow.png");
                if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERFLOW_DRAG")) {
                  graphene_rect_t from,to; const bool coords = gtk_widget_compute_bounds(first,GTK_WIDGET(probe->window->window),&from) && gtk_widget_compute_bounds(next,GTK_WIDGET(probe->window->window),&to);
                  if (!coords) { app->diagnostic_failed = true; probe->window->close(); return G_SOURCE_REMOVE; }
                  double sx,sy; gtk_native_get_surface_transform(GTK_NATIVE(probe->window->window),&sx,&sy);
                  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                  char* json = g_strdup_printf("{\"window\":%lu,\"start\":[%d,%d],\"end\":[%d,%d],\"dwell\":0.7}",gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(probe->window->window))),static_cast<int>(from.origin.x+from.size.width/2+sx),static_cast<int>(from.origin.y+from.size.height/2+sy),static_cast<int>(to.origin.x+to.size.width/2+sx),static_cast<int>(to.origin.y+to.size.height/2+sy));
                  G_GNUC_END_IGNORE_DEPRECATIONS
                  g_file_set_contents((app->screenshot+".drag.json").c_str(),json,-1,nullptr); g_free(json);
                } else g_signal_emit_by_name(next,"clicked"); break;
              }
              case 1: {
                if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERFLOW_DRAG") && !g_file_test((app->screenshot+".drag.done").c_str(),G_FILE_TEST_EXISTS) && ++probe->attempts < 20) { --probe->step; return G_SOURCE_CONTINUE; }
                const bool moved = gtk_adjustment_get_value(adjustment) > 1; g_print("Overflow scroll button diagnostic: %s\n",moved ? "PASS" : "FAIL"); if (!moved) app->diagnostic_failed = true;
                ui->activate(probe->last); break;
              }
              case 2: {
                graphene_rect_t bounds;
                const bool measured = gtk_widget_compute_bounds(ui->strip_tab(probe->last.get()),GTK_WIDGET(scroll),&bounds);
                const bool visible = measured && bounds.origin.x >= -1 && bounds.origin.x+bounds.size.width <= gtk_widget_get_width(GTK_WIDGET(scroll))+1;
                g_print("Overflow selected tab visible diagnostic: %s; x=%.0f width=%.0f viewport=%d value=%.0f\n",visible ? "PASS" : "FAIL",bounds.origin.x,bounds.size.width,gtk_widget_get_width(GTK_WIDGET(scroll)),gtk_adjustment_get_value(adjustment)); if (!visible) app->diagnostic_failed = true;
                if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERFLOW_GROUP")) {
                  auto* collapse = ui->group_button(probe->last->group_id); graphene_rect_t control;
                  const bool fixed = collapse && gtk_widget_compute_bounds(collapse,ui->strip(),&control) && control.origin.x >= 0 && control.origin.x+control.size.width <= gtk_widget_get_width(ui->strip());
                  g_print("Overflow group collapse stays visible diagnostic: %s\n",fixed ? "PASS" : "FAIL"); if (!fixed) app->diagnostic_failed = true;
                }
                app->take_screenshot(*probe->window,app->screenshot+".last.png"); probe->window->close(); return G_SOURCE_REMOVE;
              }
            }
            return G_SOURCE_CONTINUE;
          },probe,+[](gpointer p) { delete static_cast<OverflowProbe*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_DRAG_INTERACTION")) {
          struct Interaction {
            Application* app; Window* window; CefRefPtr<Browser> first, second;
            std::string id; bool overview, whole, merge, ungroup; int step = 0, attempts = 0;
          };
          auto at = [window](int index) -> CefRefPtr<Browser> { return static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs,index))),"browser")); };
          auto* test = new Interaction{app,window,at(0),at(1),"",g_getenv("SELECT_FOR_FIGMA_TEST_INTERACTION_OVERVIEW") != nullptr,g_getenv("SELECT_FOR_FIGMA_TEST_INTERACTION_WHOLE") != nullptr,g_getenv("SELECT_FOR_FIGMA_TEST_INTERACTION_MERGE") != nullptr,g_getenv("SELECT_FOR_FIGMA_TEST_INTERACTION_UNGROUP") != nullptr};
          if (!test->merge) { test->id = window->workspace->create_group("Moving group",test->first); window->workspace->assign(test->second,test->id); }
          window->workspace->activate(test->first);
          if (test->whole) window->workspace->activate(test->second);
          if (!test->merge && !test->ungroup) gtk_window_set_default_size(window->window,760,560);
          if (test->overview) window->workspace->show_overview(true);
          g_timeout_add_full(G_PRIORITY_DEFAULT,600,+[](gpointer p) -> gboolean {
            auto* test = static_cast<Interaction*>(p); auto* app = test->app; auto* window = test->window; auto* ui = window->workspace.get();
            if (test->step == 0 && test->overview && !test->whole && !test->merge) ui->expand_overview(test->id);
            if (test->step == 1) {
              auto* source = test->whole ? (test->overview ? ui->overview_group(test->id) : ui->group_button(test->id)) : (test->overview ? ui->preview_button(test->first.get()) : ui->strip_tab(test->first.get()));
              graphene_rect_t from{}, to{};
              bool measured = source && gtk_widget_compute_bounds(source,GTK_WIDGET(window->window),&from);
              if (test->merge) { auto* destination = test->overview ? ui->preview_button(test->second.get()) : ui->strip_tab(test->second.get()); measured &= destination && gtk_widget_compute_bounds(destination,GTK_WIDGET(window->window),&to); }
              if (!measured) { g_print("Interaction coordinates diagnostic: FAIL\n"); app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
              double sx,sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window),&sx,&sy);
              const int end_x = test->merge ? to.origin.x+to.size.width/2+sx : 20;
              const int end_y = test->merge ? to.origin.y+to.size.height/2+sy : 150;
              G_GNUC_BEGIN_IGNORE_DEPRECATIONS
              char* json = g_strdup_printf("{\"window\":%lu,\"start\":[%d,%d],\"end\":[%d,%d],\"dwell\":0%s}",gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))),static_cast<int>(from.origin.x+from.size.width/2+sx),static_cast<int>(from.origin.y+from.size.height/2+sy),end_x,end_y,test->merge || test->ungroup ? "" : ",\"end_root\":[1100,820]");
              G_GNUC_END_IGNORE_DEPRECATIONS
              g_file_set_contents((app->screenshot+".drag.json").c_str(),json,-1,nullptr); g_free(json);
            }
            if (test->step == 2 && !g_file_test((app->screenshot+".drag.done").c_str(),G_FILE_TEST_EXISTS) && ++test->attempts < 20) return G_SOURCE_CONTINUE;
            if (test->step == 4) {
              bool correct;
              if (test->merge) correct = !test->first->group_id.empty() && test->first->group_id == test->second->group_id && app->state.groups.size() == 1;
              else if (test->ungroup) correct = test->first->group_id.empty() && test->second->group_id == test->id && test->first->owner() == window;
              else {
                auto* destination = test->first->owner();
                correct = destination && destination != window && app->windows.size() == 2 && destination->current() == (test->whole ? test->second : test->first) && !destination->workspace->overview_open();
                if (test->whole) correct &= test->first->group_id == test->id && test->second->group_id == test->id && test->second->owner() == destination && adw_tab_view_get_n_pages(destination->tabs) == 2;
                else correct &= test->first->group_id.empty() && test->second->owner() == window && test->second->group_id == test->id;
              }
              g_print("%s %s diagnostic: %s\n",test->overview ? "Overview" : "Header",test->merge ? "drag creates group" : test->ungroup ? "drag to empty space ungroups" : test->whole ? "whole group detaches to new window" : "tab detaches to new window",correct ? "PASS" : "FAIL");
              if (!correct) app->diagnostic_failed = true;
              int index = 0;
              for (auto& item : app->windows) if (!item->destroyed && !item->closing) { app->take_screenshot(*item,app->screenshot+".window-"+std::to_string(index++)+".png"); item->close(); }
              return G_SOURCE_REMOVE;
            }
            ++test->step; return G_SOURCE_CONTINUE;
          },test,+[](gpointer p) { delete static_cast<Interaction*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_DRAG")) {
          struct GroupDragTest {
            Application* app; Window* window; CefRefPtr<Browser> first, second, third, fourth;
            std::string first_id, second_id; bool overview, members, plain, join; int step = 0, attempts = 0;
          };
          auto browser_at = [window](int index) -> CefRefPtr<Browser> {
            return static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, index))), "browser"));
          };
          auto* test = new GroupDragTest{app, window, browser_at(0), browser_at(1), browser_at(2), nullptr, "", "", g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_DRAG_OVERVIEW") != nullptr, g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_DRAG_MEMBERS") != nullptr, g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_DRAG_PLAIN") != nullptr, g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_DRAG_JOIN") != nullptr};
          test->first_id = window->workspace->create_group("Project Metro", test->first);
          window->workspace->assign(test->second, test->first_id);
          test->second_id = window->workspace->create_group("Project Lychee", test->third);
          window->workspace->toggle_group(test->second_id);
          test->fourth = window->open(Policy::new_tab_url, false, true);
          if (test->members) window->workspace->activate(test->first);
          if (test->overview) window->workspace->show_overview(true);
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer p) -> gboolean {
            auto* test = static_cast<GroupDragTest*>(p); auto* app = test->app; auto* window = test->window; auto* ui = window->workspace.get();
            switch (test->step) {
              case 0:
                if (test->overview && test->members) ui->expand_overview(test->first_id);
                break;
              case 1: {
                GtkWidget* source = test->members || test->join ? (test->overview ? ui->preview_button(test->join && !test->members ? test->fourth.get() : test->first.get()) : ui->strip_tab(test->join && !test->members ? test->fourth.get() : test->first.get())) : (test->overview ? ui->overview_group(test->first_id) : ui->group_button(test->first_id));
                GtkWidget* destination = (test->members && !test->join) || test->plain ? (test->overview ? ui->preview_button(test->plain ? test->fourth.get() : test->second.get()) : ui->strip_tab(test->plain ? test->fourth.get() : test->second.get())) : (test->overview ? ui->overview_group(test->join && !test->members ? test->first_id : test->second_id) : ui->group_button(test->join && !test->members ? test->first_id : test->second_id));
                graphene_rect_t from{}, to{};
                if (!source || !destination || !gtk_widget_compute_bounds(source, GTK_WIDGET(window->window), &from) || !gtk_widget_compute_bounds(destination, GTK_WIDGET(window->window), &to)) {
                  app->diagnostic_failed = true; g_print("Group drag coordinates diagnostic: FAIL\n"); window->close(); return G_SOURCE_REMOVE;
                }
                app->take_screenshot(*window, app->screenshot+".before.png");
                double sx, sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window), &sx, &sy);
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                char* json = g_strdup_printf("{\"window\":%lu,\"start\":[%d,%d],\"end\":[%d,%d]}", gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))), static_cast<int>(from.origin.x+from.size.width/2+sx), static_cast<int>(from.origin.y+from.size.height/2+sy), static_cast<int>(to.origin.x+to.size.width*(test->members && test->plain ? .85 : .5)+sx), static_cast<int>(to.origin.y+to.size.height/2+sy));
                G_GNUC_END_IGNORE_DEPRECATIONS
                g_file_set_contents((app->screenshot+".drag.json").c_str(), json, -1, nullptr); g_free(json);
                g_timeout_add(20, +[](gpointer p) -> gboolean {
                  auto* test = static_cast<GroupDragTest*>(p);
                  if (!g_file_test((test->app->screenshot+".drag.done").c_str(), G_FILE_TEST_EXISTS)) return test->window->closing ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
                  g_timeout_add(80, +[](gpointer p) -> gboolean {
                    auto* test = static_cast<GroupDragTest*>(p); const double progress = test->window->workspace->drop_progress();
                    const bool animated = progress > 0 && progress < 1;
                    g_print("Group drag settling diagnostic: %s; progress=%.3f\n", animated ? "PASS" : "FAIL", progress);
                    if (!animated) test->app->diagnostic_failed = true;
                    test->app->take_screenshot(*test->window, test->app->screenshot+".settling.png"); return G_SOURCE_REMOVE;
                  }, test); return G_SOURCE_REMOVE;
                }, test); break;
              }
              case 2:
                if ((!g_file_test((app->screenshot+".drag.done").c_str(), G_FILE_TEST_EXISTS) || ui->drop_progress() < 1) && ++test->attempts < 20) return G_SOURCE_CONTINUE;
                break;
              case 3: {
                auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
                auto* second = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 1))), "browser"));
                auto* third = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 2))), "browser"));
                const bool reordered = test->members && test->plain ? (first == test->second.get() && second == test->third.get() && third == test->fourth.get() && test->first->group_id.empty()) : test->members && test->join ? (first == test->second.get() && second == test->third.get() && third == test->first.get()) : test->join ? (first == test->first.get() && second == test->second.get() && third == test->fourth.get() && test->fourth->group_id == test->first_id) : test->plain ? (first == test->third.get() && second == test->fourth.get() && third == test->first.get()) : test->members ? (first == test->second.get() && second == test->first.get() && third == test->third.get()) : (first == test->third.get() && second == test->first.get() && third == test->second.get());
                const bool intact = test->first->group_id == (test->members && test->plain ? "" : test->members && test->join ? test->second_id : test->first_id) && test->second->group_id == test->first_id && test->third->group_id == test->second_id;
                g_print("%s %s reorder diagnostic: %s\n", test->overview ? "Overview" : "Header", test->members && test->plain ? "tab leaving group" : test->members && test->join ? "tab moving between groups" : test->join ? "tab joining group" : test->plain ? "group past plain tab" : test->members ? "group member" : "whole group", reordered && intact ? "PASS" : "FAIL");
                if (!reordered || !intact) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".reordered.png"); break;
              }
              default: window->close(); return G_SOURCE_REMOVE;
            }
            ++test->step; return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<GroupDragTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_GROUP_CREATION")) {
          struct GroupCreateTest { Application* app; Window* window; CefRefPtr<Browser> first; int step = 0, attempts = 0; };
          auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
          auto* test = new GroupCreateTest{app, window, first};
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer p) -> gboolean {
            auto* test = static_cast<GroupCreateTest*>(p); auto* app = test->app; auto* window = test->window;
            switch (test->step) {
              case 0: {
                const bool enabled = g_action_group_get_action_enabled(G_ACTION_GROUP(window->window), "new-tab-group");
                g_print("New group action diagnostic: %s\n", enabled ? "PASS" : "FAIL"); if (!enabled) app->diagnostic_failed = true;
                g_action_group_activate_action(G_ACTION_GROUP(window->window), "new-tab-group", nullptr); break;
              }
              case 1: {
                auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window));
                const bool naming = dialog && ADW_IS_ALERT_DIALOG(dialog) && g_object_get_data(G_OBJECT(dialog), "entry");
                g_print("New group naming dialog diagnostic: %s\n", naming ? "PASS" : "FAIL");
                if (!naming) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "entry")), "Project Menu");
                auto* picker = g_object_get_data(G_OBJECT(dialog),"group-color"); GdkRGBA color; gdk_rgba_parse(&color,"#e66100");
                const bool native = GTK_IS_COLOR_DIALOG_BUTTON(picker); g_print("Native group color picker diagnostic: %s\n",native ? "PASS" : "FAIL");
                if (native) gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(picker),&color); else app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".name-dialog.png");
                g_signal_emit_by_name(adw_dialog_get_default_widget(dialog), "clicked");
                window->workspace->toggle_group(window->current()->group_id); break;
              }
              case 2: {
                const bool created = app->state.groups.size() == 1 && app->state.groups.front().title == "Project Menu" && app->state.groups.front().color == "#e66100" && adw_tab_view_get_n_pages(window->tabs) == 4 && !window->current()->group_id.empty();
                g_print("Group creation from main menu diagnostic: %s\n", created ? "PASS" : "FAIL"); if (!created) app->diagnostic_failed = true;
                auto* tile = window->workspace->strip_tab(test->first.get()); graphene_rect_t bounds;
                if (!gtk_widget_compute_bounds(tile, GTK_WIDGET(window->window), &bounds)) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                double sx, sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window), &sx, &sy);
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                char* json = g_strdup_printf("{\"window\":%lu,\"click\":true,\"button\":3,\"start\":[%d,%d],\"end\":[0,0]}", gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))), static_cast<int>(bounds.origin.x+bounds.size.width/2+sx), static_cast<int>(bounds.origin.y+bounds.size.height/2+sy));
                G_GNUC_END_IGNORE_DEPRECATIONS
                g_file_set_contents((app->screenshot+".drag.json").c_str(), json, -1, nullptr); g_free(json); break;
              }
              case 3: {
                if (!g_file_test((app->screenshot+".drag.done").c_str(), G_FILE_TEST_EXISTS) && ++test->attempts < 12) return G_SOURCE_CONTINUE;
                auto* tile = window->workspace->strip_tab(test->first.get()); auto* popup = gtk_widget_get_last_child(tile);
                const bool context = GTK_IS_POPOVER_MENU(popup) && gtk_widget_get_visible(popup);
                g_print("Tab context menu diagnostic: %s\n", context ? "PASS" : "FAIL");
                if (!context) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                auto* item = menu_action(popup, "win.tab-group-new");
                g_print("Group menu item diagnostic: %s\n", item ? "PASS" : "FAIL");
                if (!item) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                auto* native = gtk_widget_get_native(item); graphene_rect_t bounds;
                if (!gtk_widget_compute_bounds(item, GTK_WIDGET(native), &bounds)) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                double sx, sy; gtk_native_get_surface_transform(native, &sx, &sy);
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                char* json = g_strdup_printf("{\"window\":%lu,\"click\":true,\"button\":1,\"start\":[%d,%d],\"end\":[0,0]}", gdk_x11_surface_get_xid(gtk_native_get_surface(native)), static_cast<int>(bounds.origin.x+bounds.size.width/2+sx), static_cast<int>(bounds.origin.y+bounds.size.height/2+sy));
                G_GNUC_END_IGNORE_DEPRECATIONS
                g_file_set_contents((app->screenshot+".menu.drag.json").c_str(), json, -1, nullptr); g_free(json); test->attempts = 0; break;
              }
              case 4: {
                if (!g_file_test((app->screenshot+".menu.drag.done").c_str(), G_FILE_TEST_EXISTS) && ++test->attempts < 12) return G_SOURCE_CONTINUE;
                auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window));
                g_print("Context group naming dialog diagnostic: %s\n", dialog && ADW_IS_ALERT_DIALOG(dialog) ? "PASS" : "FAIL");
                if (!dialog || !ADW_IS_ALERT_DIALOG(dialog)) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "entry")), "Project Context");
                g_signal_emit_by_name(adw_dialog_get_default_widget(dialog), "clicked"); break;
              }
              case 5: {
                const bool created = app->state.groups.size() == 2 && !test->first->group_id.empty() && adw_tab_view_get_n_pages(window->tabs) == 4;
                g_print("Group creation from context menu diagnostic: %s\n", created ? "PASS" : "FAIL"); if (!created) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".created.png"); window->workspace->show_overview(true); break;
              }
              case 6:
                g_action_group_activate_action(G_ACTION_GROUP(window->window), "new-tab-group", nullptr); break;
              case 7: {
                auto* dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window->window));
                if (!dialog || !ADW_IS_ALERT_DIALOG(dialog)) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "entry")), "Project Overview");
                g_signal_emit_by_name(adw_dialog_get_default_widget(dialog), "clicked"); break;
              }
              case 8: {
                const bool created = window->workspace->overview_open() && app->state.groups.size() == 3 && adw_tab_view_get_n_pages(window->tabs) == 5;
                g_print("Group creation in overview diagnostic: %s\n", created ? "PASS" : "FAIL"); if (!created) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".overview-created.png"); break;
              }
              default: window->close(); return G_SOURCE_REMOVE;
            }
            ++test->step; return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<GroupCreateTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_MIDDLE_CLICK")) {
          struct MiddleTest { Application* app; Window* window; CefRefPtr<Browser> first, second; int step = 0, attempts = 0; };
          auto browser_at = [window](int index) -> CefRefPtr<Browser> { return static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, index))), "browser")); };
          auto* test = new MiddleTest{app, window, browser_at(0), browser_at(1)};
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer p) -> gboolean {
            auto* test = static_cast<MiddleTest*>(p); auto* app = test->app; auto* window = test->window;
            auto request = [window, app](GtkWidget* tile, const std::string& suffix) {
              graphene_rect_t bounds; if (!gtk_widget_compute_bounds(tile, GTK_WIDGET(window->window), &bounds)) return false;
              double sx, sy; gtk_native_get_surface_transform(GTK_NATIVE(window->window), &sx, &sy);
              G_GNUC_BEGIN_IGNORE_DEPRECATIONS
              char* json = g_strdup_printf("{\"window\":%lu,\"click\":true,\"button\":2,\"start\":[%d,%d],\"end\":[0,0]}", gdk_x11_surface_get_xid(gtk_native_get_surface(GTK_NATIVE(window->window))), static_cast<int>(bounds.origin.x+bounds.size.width/2+sx), static_cast<int>(bounds.origin.y+bounds.size.height/2+sy));
              G_GNUC_END_IGNORE_DEPRECATIONS
              g_file_set_contents((app->screenshot+suffix+".drag.json").c_str(), json, -1, nullptr); g_free(json); return true;
            };
            switch (test->step) {
              case 0:
                if (!request(window->workspace->strip_tab(test->first.get()), "")) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                break;
              case 1: {
                if (!g_file_test((app->screenshot+".drag.done").c_str(), G_FILE_TEST_EXISTS) && ++test->attempts < 12) return G_SOURCE_CONTINUE;
                const bool closed = test->first->closing && adw_tab_view_get_n_pages(window->tabs) == 2 && window->current()->is_home;
                g_print("Header middle-click close diagnostic: %s\n", closed ? "PASS" : "FAIL"); if (!closed) app->diagnostic_failed = true;
                window->workspace->show_overview(true); break;
              }
              case 2:
                if (!request(window->workspace->preview_button(test->second.get()), ".overview")) { app->diagnostic_failed = true; window->close(); return G_SOURCE_REMOVE; }
                test->attempts = 0; break;
              case 3: {
                if (!g_file_test((app->screenshot+".overview.drag.done").c_str(), G_FILE_TEST_EXISTS) && ++test->attempts < 12) return G_SOURCE_CONTINUE;
                const bool closed = test->second->closing && adw_tab_view_get_n_pages(window->tabs) == 1 && window->workspace->overview_open();
                g_print("Overview middle-click close diagnostic: %s\n", closed ? "PASS" : "FAIL"); if (!closed) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".closed.png"); break;
              }
              default: window->close(); return G_SOURCE_REMOVE;
            }
            ++test->step; return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<MiddleTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_TAB_CHROME")) {
          struct TabChromeTest { Application* app; Window* window; CefRefPtr<Browser> first; int initial = 0, expanded = 0, middle = 0, step = 0; };
          auto* first = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, 0))), "browser"));
          window->workspace->activate(first);
          auto* test = new TabChromeTest{app, window, first};
          g_timeout_add_full(G_PRIORITY_DEFAULT, 500, +[](gpointer p) -> gboolean {
            auto* test = static_cast<TabChromeTest*>(p); auto* window = test->window; auto* app = test->app; auto* tab = window->workspace->strip_tab(test->first.get());
            switch (test->step++) {
              case 0: {
                test->initial = gtk_widget_get_width(tab);
                auto* overlay = gtk_widget_get_first_child(tab); auto* main = gtk_overlay_get_child(GTK_OVERLAY(overlay)); auto* title = gtk_button_get_child(GTK_BUTTON(main));
                auto* close = gtk_widget_get_last_child(overlay); auto* icon = gtk_button_get_child(GTK_BUTTON(close)); graphene_rect_t bounds;
                const bool centered = gtk_widget_compute_bounds(title, tab, &bounds) && std::abs(bounds.origin.x+bounds.size.width/2-test->initial/2.) < 1;
                const bool circle = gtk_widget_get_width(icon) == 24 && gtk_widget_get_height(icon) == 24 && gtk_widget_get_width(close) == 34;
                g_print("Centered tab title diagnostic: %s\n", centered ? "PASS" : "FAIL"); g_print("Circular close button diagnostic: %s\n", circle ? "PASS" : "FAIL");
                if (!centered || !circle) app->diagnostic_failed = true;
                gtk_widget_set_state_flags(close, GTK_STATE_FLAG_PRELIGHT, false);
                g_timeout_add(90, +[](gpointer p) -> gboolean {
                  auto* test = static_cast<TabChromeTest*>(p); test->app->take_screenshot(*test->window, test->app->screenshot+".close-hover.png");
                  adw_tab_view_close_page(test->window->tabs, adw_tab_view_get_nth_page(test->window->tabs, 2));
                  g_timeout_add(65, +[](gpointer p) -> gboolean {
                    auto* test = static_cast<TabChromeTest*>(p); test->middle = gtk_widget_get_width(test->window->workspace->strip_tab(test->first.get()));
                    test->app->take_screenshot(*test->window, test->app->screenshot+".closing.png"); return G_SOURCE_REMOVE;
                  }, test); return G_SOURCE_REMOVE;
                }, test); break;
              }
              case 1: {
                test->expanded = gtk_widget_get_width(tab); const bool animated = test->initial < test->middle && test->middle < test->expanded;
                g_print("Tab close resize diagnostic: %s; widths=%d → %d → %d\n", animated ? "PASS" : "FAIL", test->initial, test->middle, test->expanded); if (!animated) app->diagnostic_failed = true;
                window->workspace->new_tab();
                g_timeout_add(65, +[](gpointer p) -> gboolean {
                  auto* test = static_cast<TabChromeTest*>(p); test->middle = gtk_widget_get_width(test->window->workspace->strip_tab(test->first.get()));
                  test->app->take_screenshot(*test->window, test->app->screenshot+".adding.png"); return G_SOURCE_REMOVE;
                }, test); break;
              }
              case 2: {
                const int final = gtk_widget_get_width(tab); const bool animated = final < test->middle && test->middle < test->expanded;
                g_print("Tab add resize diagnostic: %s; widths=%d → %d → %d\n", animated ? "PASS" : "FAIL", test->expanded, test->middle, final); if (!animated) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".resized.png"); break;
              }
              default: window->close(); return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<TabChromeTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_OVERVIEW_NEW_TAB")) {
          struct NewTabTest { Application* app; Window* window; CefRefPtr<Browser> previous; int step = 0; };
          window->open("https://www.figma.com/design/0");
          auto* test = new NewTabTest{app, window, window->current()}; window->workspace->show_overview(true);
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer p) -> gboolean {
            auto* test = static_cast<NewTabTest*>(p); auto* app = test->app; auto* window = test->window;
            switch (test->step++) {
              case 0:
                g_signal_emit_by_name(window->workspace->new_tab_button(), "clicked");
                g_timeout_add(120, +[](gpointer p) -> gboolean {
                  auto* test = static_cast<NewTabTest*>(p); auto* ui = test->window->workspace.get();
                  const double progress = ui->transition_progress(); const bool origin = ui->transition_origin() == ui->new_tab_button() && progress > 0 && progress < 1 && test->window->current() == test->previous;
                  g_print("Overview new tab origin diagnostic: %s; progress=%.3f\n", origin ? "PASS" : "FAIL", progress); if (!origin) test->app->diagnostic_failed = true;
                  test->app->take_screenshot(*test->window, test->app->screenshot+".new-tab-zoom.png"); return G_SOURCE_REMOVE;
                }, test); break;
              case 1: {
                const bool selected = !window->workspace->overview_open() && window->current() != test->previous && Policy::is_new_tab(window->current()->url) && adw_tab_view_get_n_pages(window->tabs) == 4;
                g_print("Overview new tab selection diagnostic: %s\n", selected ? "PASS" : "FAIL"); if (!selected) app->diagnostic_failed = true; break;
              }
              default: window->close(); return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<NewTabTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_DOWNLOADS")) {
          const auto path = app->state.directory()+"/Gamepad Keyboard.svg";
          const char* fixture = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"/>";
          g_file_set_contents(path.c_str(), fixture, -1, nullptr); app->state.exports.push_back({path, "https://www.figma.com/design/0", "Project Metro", "14.10.2026"});
          window->show_downloads();
          g_timeout_add(800, +[](gpointer p) -> gboolean {
            auto* app = static_cast<Application*>(p); if (auto* window = app->active_window()) {
              const bool icon = gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(gdk_display_get_default()), "external-link-symbolic");
              g_print("Download external-link icon diagnostic: %s\n", icon ? "PASS" : "FAIL"); if (!icon) app->diagnostic_failed = true;
              app->take_screenshot(*window, app->screenshot+".downloads.png"); window->close();
            } return G_SOURCE_REMOVE;
          }, app); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_HOME_CARD")) {
          struct HomeTest { Application* app; Window* window; CefRefPtr<Browser> home; AdwTabPage* document; int step = 0, attempts = 0; };
          auto home = window->current(); window->open("https://www.figma.com/design/0");
          auto* test = new HomeTest{app, window, home, adw_tab_view_get_selected_page(window->tabs)}; window->workspace->show_overview(true);
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer p) -> gboolean {
            auto* test = static_cast<HomeTest*>(p); auto* app = test->app; auto* window = test->window; auto* ui = window->workspace.get();
            switch (test->step++) {
              case 0: {
                auto* card = ui->home_card(); const bool shape = card && gtk_widget_get_width(card) > gtk_widget_get_height(card)*4 && gtk_widget_get_height(card) <= 56;
                g_print("Home card shape diagnostic: %s; %dx%d px\n", shape ? "PASS" : "FAIL", gtk_widget_get_width(card), gtk_widget_get_height(card));
                if (!shape) app->diagnostic_failed = true;
                app->take_screenshot(*window, app->screenshot+".overview.png");
                const auto before = g_get_monotonic_time();
                g_signal_emit_by_name(card, "clicked");
                const auto elapsed = (g_get_monotonic_time()-before)/1000;
                g_print("Home click diagnostic: %s; handler=%lld ms\n", elapsed < 50 ? "PASS" : "FAIL", static_cast<long long>(elapsed)); if (elapsed >= 50) app->diagnostic_failed = true;
                g_timeout_add(120, +[](gpointer p) -> gboolean {
                  auto* test = static_cast<HomeTest*>(p); const auto progress = test->window->workspace->transition_progress(); const bool zoom = progress > 0 && progress < 1;
                  g_print("Home zoom diagnostic: %s; progress=%.3f\n", zoom ? "PASS" : "FAIL", progress); if (!zoom) test->app->diagnostic_failed = true;
                  test->app->take_screenshot(*test->window, test->app->screenshot+".home-zoom.png"); return G_SOURCE_REMOVE;
                }, test); break;
              }
              case 1: {
                if (ui->transition_progress() > 0 && ++test->attempts < 10) { --test->step; return G_SOURCE_CONTINUE; }
                const bool preserved = window->current().get() == test->home.get() && window->current()->is_home && !ui->overview_open() && adw_tab_view_get_selected_page(window->tabs) == test->document && adw_tab_view_get_n_pages(window->tabs) == 3;
                g_print("Home card selection diagnostic: %s\n", preserved ? "PASS" : "FAIL"); if (!preserved) app->diagnostic_failed = true;
                ui->show_overview(true); break;
              }
              case 2: app->take_screenshot(*window, app->screenshot+".home-active.png"); break;
              default: for (auto& window : app->windows) if (!window->destroyed) window->close(); return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<HomeTest*>(p); }); return G_SOURCE_REMOVE;
        } else if (g_getenv("SELECT_FOR_FIGMA_TEST_GROUPS")) {
          struct TestGroups { Application* app; Window* window; std::string id; CefRefPtr<Browser> first, second, third; std::string other; int step = 0; };
          auto browser_at = [window](int index) -> CefRefPtr<Browser> {
            return static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(adw_tab_view_get_nth_page(window->tabs, index))), "browser"));
          };
          auto* test = new TestGroups{app, window, "", browser_at(0), browser_at(1), browser_at(2), ""};
          test->id = window->workspace->create_group("Project Metro", test->first); window->workspace->assign(test->third, test->id);
          const bool ordered = adw_tab_view_get_page_position(window->tabs, adw_tab_view_get_page(window->tabs, test->first->widget())) == 0 && adw_tab_view_get_page_position(window->tabs, adw_tab_view_get_page(window->tabs, test->third->widget())) == 1;
          g_print("Group keyboard order diagnostic: %s\n", ordered ? "PASS" : "FAIL"); if (!ordered) app->diagnostic_failed = true;
          window->workspace->assign(test->third, ""); window->workspace->assign(test->second, test->id);
          window->workspace->activate(test->first); window->workspace->activate(test->second);
          g_timeout_add_full(G_PRIORITY_DEFAULT, 600, +[](gpointer data) -> gboolean {
            auto* test = static_cast<TestGroups*>(data); auto* ui = test->window->workspace.get(); auto* app = test->app;
            const auto screenshot = [&](const char* suffix) { if (!app->screenshot.empty()) app->take_screenshot(*test->window, app->screenshot+suffix); };
            switch (test->step++) {
              case 0: {
                screenshot(".expanded.png");
                auto* collapse = ui->group_button(test->id); auto* members = gtk_widget_get_parent(ui->strip_tab(test->first.get())); auto* container = gtk_widget_get_parent(members);
                auto* content = gtk_widget_get_parent(container);
                const bool exclusive = !gtk_widget_get_next_sibling(container) && !gtk_widget_get_prev_sibling(container) &&
                  std::abs(gtk_widget_get_width(container)+4-gtk_widget_get_width(content)) <= 4 && !ui->strip_tab(test->third.get());
                const bool square = gtk_widget_get_width(collapse) == 34 && gtk_widget_get_height(collapse) == gtk_widget_get_height(ui->strip_tab(test->first.get()));
                const bool at_left = gtk_widget_get_first_child(ui->strip()) == collapse;
                const bool full_size = gtk_widget_get_height(ui->strip_tab(test->first.get())) == 34;
                const bool title_hidden = gtk_widget_get_first_child(container) == members && !gtk_widget_get_next_sibling(members);
                auto* header = GTK_WIDGET(g_object_get_data(G_OBJECT(test->window->window),"main-header"));
                const bool tinted = gtk_widget_has_css_class(header,"select-group-header");
                g_print("Group title hidden in header diagnostic: %s\n",title_hidden ? "PASS" : "FAIL");
                g_print("Whole header group tint diagnostic: %s\n",tinted ? "PASS" : "FAIL");
                g_print("Expanded group full header diagnostic: %s\n", exclusive ? "PASS" : "FAIL");
                g_print("Square group collapse diagnostic: %s; %dx%d\n", square ? "PASS" : "FAIL", gtk_widget_get_width(collapse), gtk_widget_get_height(collapse));
                g_print("Group collapse at left edge diagnostic: %s\n", at_left ? "PASS" : "FAIL");
                g_print("Full size group members diagnostic: %s\n", full_size ? "PASS" : "FAIL");
                if (!exclusive || !square || !full_size || !at_left || !title_hidden || !tinted) app->diagnostic_failed = true;
                g_signal_emit_by_name(collapse, "clicked"); break;
              }
              case 1: {
                screenshot(".collapsed.png"); auto* expand = ui->group_button(test->id); auto* normal = ui->strip_tab(test->third.get());
                graphene_rect_t group_bounds{}, normal_bounds{};
                const bool equal = GTK_IS_BUTTON(expand) && normal && gtk_widget_compute_bounds(expand, ui->strip(), &group_bounds) &&
                  gtk_widget_compute_bounds(normal, ui->strip(), &normal_bounds) && std::abs(group_bounds.size.width-normal_bounds.size.width) <= 1 && gtk_widget_get_height(normal) == 34;
                g_print("Collapsed group equal fraction diagnostic: %s; group=%.0f normal=%.0f\n", equal ? "PASS" : "FAIL", group_bounds.size.width, normal_bounds.size.width);
                graphene_rect_t contents{};
                const bool centered = gtk_widget_compute_bounds(gtk_button_get_child(GTK_BUTTON(expand)), ui->strip(), &contents) &&
                  std::abs(contents.origin.x+contents.size.width/2-group_bounds.origin.x-group_bounds.size.width/2) <= 1;
                g_print("Collapsed group content centered diagnostic: %s\n", centered ? "PASS" : "FAIL");
                if (!equal || !centered) app->diagnostic_failed = true;
                ui->activate(test->third); test->window->native_home();
                g_signal_emit_by_name(ui->group_button(test->id), "clicked"); break;
              }
              case 2: {
                const bool restored = test->window->current().get() == test->second.get() && !ui->strip_tab(test->third.get());
                g_print("Group last-used tab diagnostic: %s\n", restored ? "PASS" : "FAIL"); if (!restored) app->diagnostic_failed = true;
                ui->show_overview(true); break;
              }
              case 3: {
                screenshot(".overview.png"); auto* card = ui->overview_group(test->id);
                auto* normal = gtk_widget_get_ancestor(ui->preview_button(test->third.get()), GTK_TYPE_BOX);
                auto* content = gtk_button_get_child(GTK_BUTTON(card));
                graphene_rect_t group_bounds{}, normal_bounds{};
                const bool aligned = gtk_widget_compute_bounds(card, ui->overview(), &group_bounds) && gtk_widget_compute_bounds(normal, ui->overview(), &normal_bounds) &&
                  std::abs(group_bounds.origin.y-normal_bounds.origin.y) < 1 && std::abs(group_bounds.size.height-normal_bounds.size.height) < 1 &&
                  GTK_IS_LABEL(gtk_widget_get_last_child(content));
                g_print("Overview group alignment diagnostic: %s; group=%.0f normal=%.0f\n", aligned ? "PASS" : "FAIL", group_bounds.size.height, normal_bounds.size.height);
                if (!aligned) app->diagnostic_failed = true;
                g_signal_emit_by_name(card, "clicked"); break;
              }
              case 4: screenshot(".drawer.png"); ui->activate(test->first); break;
              case 5: {
                const bool selected = test->window->current().get() == test->first.get() && !ui->overview_open();
                g_print("Tab groups diagnostic: %s\n", selected ? "PASS" : "FAIL"); if (!selected) app->diagnostic_failed = true;
                adw_tab_view_close_page(test->window->tabs, adw_tab_view_get_page(test->window->tabs, test->first->widget())); break;
              }
              case 6: {
                test->window->native_home(); ui->toggle_group(test->id);
                g_signal_emit_by_name(ui->group_button(test->id), "clicked");
                const bool fallback = test->first->closing && test->window->current().get() == test->second.get();
                g_print("Closed group member fallback diagnostic: %s\n", fallback ? "PASS" : "FAIL"); if (!fallback) app->diagnostic_failed = true;
                break;
              }
              case 7:
                test->other = ui->create_group("Other", test->third); ui->toggle_group(test->other); break;
              case 8: {
                auto* first = ui->group_button(test->id); auto* other = ui->group_button(test->other);
                graphene_rect_t first_bounds{}, other_bounds{};
                const bool equal = first && other && gtk_widget_compute_bounds(first, ui->strip(), &first_bounds) &&
                  gtk_widget_compute_bounds(other, ui->strip(), &other_bounds) && std::abs(first_bounds.size.width-other_bounds.size.width) <= 1;
                g_print("Multiple collapsed groups equal fraction diagnostic: %s\n", equal ? "PASS" : "FAIL"); if (!equal) app->diagnostic_failed = true;
                if (first) g_signal_emit_by_name(first, "clicked");
                const bool switched = test->window->current().get() == test->second.get() && !ui->group_button(test->other);
                g_print("Switch group restores history diagnostic: %s\n", switched ? "PASS" : "FAIL"); if (!switched) app->diagnostic_failed = true;
                break;
              }
              default:
                for (auto& w : app->windows) if (!w->destroyed) w->close();
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          }, test, +[](gpointer p) { delete static_cast<TestGroups*>(p); });
          return G_SOURCE_REMOVE;
        } else {
          if (g_getenv("SELECT_FOR_FIGMA_TEST_LOGIN_SEARCH")) {
            auto* dialog = window->show_login_picker(window->current(), "https://www.figma.com/login", {{"Personal", "alice@example.invalid", ""}, {"Work", "bob@example.invalid", ""}, {"Żółć", "unicode@example.invalid", ""}});
            auto* search = GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "login-search"));
            auto* list = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "login-list"));
            bool search_passed = true;
            for (const auto* query : {"BOB", "żółć", "missing", ""}) {
              gtk_editable_set_text(search, query); g_signal_emit_by_name(search, "search-changed");
              int count = 0;
              for (GtkWidget* row = gtk_widget_get_first_child(list); row; row = gtk_widget_get_next_sibling(row)) if (GTK_IS_LIST_BOX_ROW(row) && gtk_widget_get_child_visible(row)) ++count;
              const int expected = !*query ? 3 : !std::strcmp(query, "missing") ? 0 : 1;
              if (count != expected) search_passed = false;
            }
            gtk_editable_set_text(search, "BOB"); g_signal_emit_by_name(search, "search-changed");
            if (!search_passed) app->diagnostic_failed = true;
            g_print("KeePassXC search diagnostic: %s\n", search_passed ? "PASS" : "FAIL");
          } else if (g_getenv("SELECT_FOR_FIGMA_TEST_SHORTCUTS")) window->show_shortcuts();
          else if (g_getenv("SELECT_FOR_FIGMA_TEST_ABOUT")) window->show_about();
          else window->show_preferences();
          g_print("Native diagnostic: Home, tabs and dialog presented\n");
        }
      }
      g_timeout_add_seconds(g_getenv("SELECT_FOR_FIGMA_TEST_RENDER_OVERVIEW") ? 4 : 1, +[](gpointer p) -> gboolean {
        auto* app = static_cast<Application*>(p);
        if (app->smoke_test && (g_getenv("SELECT_FOR_FIGMA_TEST_LOGIN_SEARCH") || g_getenv("SELECT_FOR_FIGMA_TEST_SHORTCUTS") || g_getenv("SELECT_FOR_FIGMA_TEST_ABOUT")) && !app->screenshot.empty())
          if (auto* window = app->active_window()) {
            if (g_getenv("SELECT_FOR_FIGMA_TEST_SHORTCUTS")) {
              auto* reference = static_cast<GWeakRef*>(g_object_get_data(G_OBJECT(window->window), "shortcuts-window"));
              if (auto* shortcuts = G_OBJECT(g_weak_ref_get(reference))) {
                app->take_screenshot(GTK_WINDOW(shortcuts), app->screenshot + ".shortcuts.png"); g_object_unref(shortcuts);
              }
            } else app->take_screenshot(*window, app->screenshot + ".picker.png");
          }
        for (auto& w : app->windows) if (!w->destroyed) w->close();
        return G_SOURCE_REMOVE;
      }, app);
      return G_SOURCE_REMOVE;
    }, &app);
  }
  const int result = app.run(argc, argv);
  app.cef_ready = false;
  engine->stop(); CefShutdown(); engine->stop();
  return app.diagnostic_failed ? 1 : result;
}
