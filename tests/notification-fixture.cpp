#include <gio/gio.h>
#include <cstring>

// Only run on check-ui.sh's private D-Bus session. It never displays a banner.
int main(int argc, char** argv) {
  if (argc != 2 || !g_getenv("FIGMA_GTK_PRIVATE_BUS")) return 1;
  const char* xml = R"XML(<node><interface name="org.freedesktop.Notifications">
    <method name="GetCapabilities"><arg type="as" direction="out"/></method>
    <method name="GetServerInformation"><arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/></method>
    <method name="Notify"><arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/><arg type="a{sv}" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="out"/></method>
    <method name="CloseNotification"><arg type="u" direction="in"/></method>
    <signal name="NotificationClosed"><arg type="u"/><arg type="u"/></signal>
    <signal name="ActionInvoked"><arg type="u"/><arg type="s"/></signal>
  </interface></node>)XML";
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!bus) { g_printerr("%s\n", error->message); g_error_free(error); return 1; }
  auto* node = g_dbus_node_info_new_for_xml(xml, nullptr);
  static const GDBusInterfaceVTable methods = {+[](GDBusConnection*, const char*, const char*, const char*, const char* method, GVariant* params, GDBusMethodInvocation* invocation, gpointer) {
    if (!std::strcmp(method, "GetCapabilities")) {
      const char* capabilities[] = {"actions", "body", "persistence", nullptr};
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(^as)", capabilities));
    } else if (!std::strcmp(method, "GetServerInformation"))
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "Private fixture", "Figma GTK", "1.0", "1.2"));
    else if (!std::strcmp(method, "CloseNotification")) g_dbus_method_invocation_return_value(invocation, nullptr);
    else if (!std::strcmp(method, "Notify")) {
      GVariant* summary = g_variant_get_child_value(params, 3);
      const char* title = g_variant_get_string(summary, nullptr);
      if (!std::strcmp(title, "Figma GTK notification diagnostic")) g_print("Chromium notification received\n");
      if (!std::strcmp(title, "Native notification diagnostic")) g_print("Native notification received\n");
      g_variant_unref(summary);
      static guint id = 0; g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", ++id));
    }
  }, nullptr, nullptr, {nullptr}};
  if (!g_dbus_connection_register_object(bus, "/org/freedesktop/Notifications", node->interfaces[0], &methods, nullptr, nullptr, &error)) {
    g_printerr("%s\n", error->message); g_error_free(error); return 1;
  }
  g_bus_own_name_on_connection(bus, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
    +[](GDBusConnection*, const char*, gpointer path) { g_file_set_contents(static_cast<const char*>(path), "ready", -1, nullptr); }, nullptr, argv[1], nullptr);
  GMainLoop* loop = g_main_loop_new(nullptr, false); g_main_loop_run(loop);
  g_main_loop_unref(loop); g_dbus_node_info_unref(node); g_object_unref(bus); return 0;
}
