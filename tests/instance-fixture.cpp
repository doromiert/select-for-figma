#include <gio/gio.h>
#include <cstring>
#include "build-info.hpp"

// A private primary instance lets us verify remote activation without opening
// another editor or touching a desktop session.
int main(int argc, char** argv) {
  if (argc != 3 || g_strcmp0(g_getenv("SELECT_FOR_FIGMA_PRIVATE_BUS"), "1")) return 2;
  const bool same = !std::strcmp(argv[1], "same");
  auto* app = g_application_new("io.github.doromiert.SelectForFigma.Diagnostic", G_APPLICATION_IS_SERVICE);
  if (same) {
    auto* action = g_simple_action_new_stateful("build-id", nullptr, g_variant_new_string(APP_BUILD_ID));
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
    g_object_unref(action);
  }
  g_signal_connect(app, "startup", G_CALLBACK(+[](GApplication* instance, gpointer ready) {
    g_application_hold(instance);
    g_file_set_contents(static_cast<const char*>(ready), "ready", -1, nullptr);
  }), argv[2]);
  g_signal_connect(app, "activate", G_CALLBACK(+[](GApplication*, gpointer) {
    g_print("Forwarded activation: PASS\n");
  }), nullptr);
  int result = g_application_run(app, 1, argv);
  g_object_unref(app);
  return result;
}
