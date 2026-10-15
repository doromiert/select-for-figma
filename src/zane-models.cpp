#include "zane-models.hpp"
#include "application.hpp"
#include <json-glib/json-glib.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
std::string text(JsonObject* object, const char* key) {
  auto* node = object ? json_object_get_member(object, key) : nullptr;
  return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : "";
}
std::string quote(const std::string& value) {
  auto* node = json_node_new(JSON_NODE_VALUE); json_node_set_string(node, value.c_str());
  char* encoded = json_to_string(node, false); std::string result(encoded); g_free(encoded); json_node_free(node); return result;
}
struct Model { std::string id, label; std::vector<std::string> efforts; };
struct Picker : std::enable_shared_from_this<Picker> {
  Window& window;
  bool work;
  std::string& model_setting() { return work ? window.app.state.zane_work_model : window.app.state.zane_chat_model; }
  std::string& effort_setting() { return work ? window.app.state.zane_work_effort : window.app.state.zane_chat_effort; }
  AdwComboRow* model = ADW_COMBO_ROW(adw_combo_row_new());
  AdwComboRow* effort = ADW_COMBO_ROW(adw_combo_row_new());
  GSubprocess* process = nullptr;
  GDataInputStream* output = nullptr;
  GCancellable* cancellation = g_cancellable_new();
  std::vector<Model> models;
  std::vector<std::string> efforts;
  bool updating = false, finished = false, held = false;
  guint timeout = 0;
  int sequence = 0, pending = 0;
  explicit Picker(Window& owner, bool for_work) : window(owner), work(for_work) { g_object_ref_sink(model); g_object_ref_sink(effort); }
  ~Picker() { stop(); g_clear_object(&output); g_clear_object(&process); g_clear_object(&cancellation); g_object_unref(model); g_object_unref(effort); }
  void stop() {
    if (timeout) { g_source_remove(timeout); timeout = 0; }
    g_cancellable_cancel(cancellation);
    if (process) g_subprocess_force_exit(process);
    if (held) { held = false; g_application_release(G_APPLICATION(window.app.gtk_app)); }
  }
  void fail(const char* message) {
    if (finished) return;
    finished = true;
    if (!window.destroyed) adw_action_row_set_subtitle(ADW_ACTION_ROW(model), message);
    stop();
  }
  void populate() {
    updating = true;
    auto* labels = gtk_string_list_new(nullptr); gtk_string_list_append(labels, "Use Codex Settings");
    guint selection = 0;
    for (unsigned i = 0; i < models.size(); ++i) {
      gtk_string_list_append(labels, models[i].label.c_str());
      if (models[i].id == model_setting()) selection = i + 1;
    }
    if (!model_setting().empty() && !selection) {
      models.push_back({model_setting(), model_setting() + " (unavailable)", {}});
      gtk_string_list_append(labels, models.back().label.c_str()); selection = models.size();
    }
    adw_combo_row_set_model(model, G_LIST_MODEL(labels)); g_object_unref(labels);
    adw_combo_row_set_selected(model, selection); gtk_widget_set_sensitive(GTK_WIDGET(model), true);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(model), work ? "For organization, edits and complex tasks" : "For conversation and straightforward questions");
    populate_efforts(false); updating = false;
  }
  void populate_efforts(bool model_changed) {
    const guint selection = adw_combo_row_get_selected(model);
    efforts = {""};
    if (selection && selection <= models.size()) for (const auto& value : models[selection - 1].efforts) efforts.push_back(value);
    auto* labels = gtk_string_list_new(nullptr); gtk_string_list_append(labels, selection ? "Model Default" : "Use Codex Settings");
    guint effort_selection = 0;
    for (unsigned i = 1; i < efforts.size(); ++i) {
      auto label = efforts[i]; if (!label.empty()) label[0] = g_ascii_toupper(label[0]);
      if (efforts[i] == "xhigh") label = "Extra High";
      gtk_string_list_append(labels, label.c_str());
      if (efforts[i] == effort_setting()) effort_selection = i;
    }
    adw_combo_row_set_model(effort, G_LIST_MODEL(labels)); g_object_unref(labels);
    adw_combo_row_set_selected(effort, effort_selection);
    gtk_widget_set_sensitive(GTK_WIDGET(effort), efforts.size() > 1);
    if (model_changed) effort_setting() = efforts[effort_selection];
  }
  void send(const std::string& message) {
    GError* error = nullptr; const auto data = message + "\n";
    if (!g_output_stream_write_all(g_subprocess_get_stdin_pipe(process), data.data(), data.size(), nullptr, cancellation, &error)) fail("Could not connect to Codex");
    g_clear_error(&error);
  }
  void list(const std::string& cursor = "") {
    pending = ++sequence;
    send("{\"id\":" + std::to_string(pending) + ",\"method\":\"model/list\",\"params\":{\"includeHidden\":false,\"limit\":100" + (cursor.empty() ? "" : ",\"cursor\":" + quote(cursor)) + "}}");
  }
  void read() {
    if (finished) return;
    auto* keep = new std::shared_ptr<Picker>(shared_from_this());
    g_data_input_stream_read_line_async(output, G_PRIORITY_DEFAULT, cancellation, +[](GObject* stream, GAsyncResult* result, gpointer data) {
      auto self = *static_cast<std::shared_ptr<Picker>*>(data); delete static_cast<std::shared_ptr<Picker>*>(data);
      gsize size; GError* error = nullptr; char* line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(stream), result, &size, &error);
      if (!self->finished && line && size < 1024 * 1024) {
        auto* parser = json_parser_new();
        if (json_parser_load_from_data(parser, line, size, nullptr) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) self->receive(json_node_get_object(json_parser_get_root(parser)));
        g_object_unref(parser);
      } else if (!self->finished) self->fail("Could not load models. Reopen Preferences to retry.");
      g_free(line); g_clear_error(&error); self->read();
    }, keep);
  }
  void receive(JsonObject* object) {
    auto* id = json_object_get_member(object, "id");
    if (!id || !JSON_NODE_HOLDS_VALUE(id) || json_node_get_value_type(id) != G_TYPE_INT64 || json_node_get_int(id) != pending) return;
    auto* result = json_object_get_member(object, "result");
    if (!result || !JSON_NODE_HOLDS_OBJECT(result)) { fail("Codex could not load its model catalog"); return; }
    if (pending == 1) { send("{\"method\":\"initialized\",\"params\":{}}"); list(); return; }
    auto* catalog = json_node_get_object(result); auto* entries = json_object_get_member(catalog, "data");
    if (!entries || !JSON_NODE_HOLDS_ARRAY(entries)) { fail("Codex returned an invalid model catalog"); return; }
    auto* array = json_node_get_array(entries);
    for (unsigned i = 0; i < json_array_get_length(array) && models.size() < 200; ++i) {
      auto* node = json_array_get_element(array, i); if (!JSON_NODE_HOLDS_OBJECT(node)) continue;
      auto* entry = json_node_get_object(node); Model candidate{text(entry, "model"), text(entry, "displayName"), {}};
      if (candidate.id.empty()) candidate.id = text(entry, "id");
      if (candidate.id.empty() || candidate.id.size() > 200 || std::any_of(models.begin(), models.end(), [&](const Model& item) { return item.id == candidate.id; })) continue;
      if (candidate.label.empty()) candidate.label = candidate.id;
      auto* options = json_object_get_member(entry, "supportedReasoningEfforts");
      if (options && JSON_NODE_HOLDS_ARRAY(options)) {
        auto* values = json_node_get_array(options);
        for (unsigned n = 0; n < json_array_get_length(values); ++n) {
          auto* option = json_array_get_element(values, n); if (!JSON_NODE_HOLDS_OBJECT(option)) continue;
          auto value = text(json_node_get_object(option), "reasoningEffort");
          if (!value.empty() && value.size() <= 32) candidate.efforts.push_back(value);
        }
      }
      models.push_back(std::move(candidate));
    }
    const auto next = text(catalog, "nextCursor");
    if (!next.empty() && models.size() < 200) { list(next); return; }
    finished = true; if (!window.destroyed) populate(); stop();
  }
  void start() {
    char* found = g_find_program_in_path("codex");
    const char* fake = window.app.render_test ? g_getenv("FIGMA_GTK_TEST_CODEX") : nullptr;
    if (!found && !fake) { fail("Install Codex and run codex login to choose a model"); return; }
    auto* launcher = g_subprocess_launcher_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE));
    GError* error = nullptr;
    process = g_subprocess_launcher_spawn(launcher, &error, fake ? fake : found, "app-server", "--listen", "stdio://", "--disable", "shell_tool", "--disable", "apps", "--disable", "plugins", "--disable", "multi_agent", "-c", "mcp_servers={}", nullptr);
    g_object_unref(launcher); g_free(found); g_clear_error(&error);
    if (!process) { fail("Could not start Codex to load models"); return; }
    held = true; g_application_hold(G_APPLICATION(window.app.gtk_app));
    output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    pending = ++sequence;
    send("{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"select_zane_models\",\"version\":\"0.1.0\"},\"capabilities\":{\"experimentalApi\":true}}}");
    timeout = g_timeout_add_seconds(10, +[](gpointer data) -> gboolean {
      auto* self = static_cast<Picker*>(data); self->timeout = 0; self->fail("Loading models timed out. Reopen Preferences to retry."); return G_SOURCE_REMOVE;
    }, this);
    read();
  }
};
}

namespace {
void add_picker(Window& window, AdwPreferencesGroup* group, bool work) {
  auto picker = std::make_shared<Picker>(window, work);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(picker->model), work ? "Work Model" : "Chat Model");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(picker->model), "Loading models from Codex…");
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(picker->effort), work ? "Work Reasoning" : "Chat Reasoning");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(picker->effort), "Lower levels favor speed; higher levels think longer");
  gtk_widget_set_sensitive(GTK_WIDGET(picker->model), false); gtk_widget_set_sensitive(GTK_WIDGET(picker->effort), false);
  adw_preferences_group_add(group, GTK_WIDGET(picker->model)); adw_preferences_group_add(group, GTK_WIDGET(picker->effort));
  g_object_set_data_full(G_OBJECT(group), work ? "zane-work-models" : "zane-chat-models", new std::shared_ptr<Picker>(picker), +[](gpointer data) { delete static_cast<std::shared_ptr<Picker>*>(data); });
  g_signal_connect(picker->model, "notify::selected", G_CALLBACK(+[](AdwComboRow* row, GParamSpec*, Picker* self) {
    if (self->updating || self->window.destroyed) return;
    const auto selected = adw_combo_row_get_selected(row); if (selected > self->models.size()) return;
    self->model_setting() = selected ? self->models[selected - 1].id : "";
    self->updating = true; self->populate_efforts(true); self->updating = false; self->window.app.state.save();
  }), picker.get());
  g_signal_connect(picker->effort, "notify::selected", G_CALLBACK(+[](AdwComboRow* row, GParamSpec*, Picker* self) {
    if (self->updating || self->window.destroyed) return;
    const auto selected = adw_combo_row_get_selected(row); if (selected >= self->efforts.size()) return;
    self->effort_setting() = self->efforts[selected]; self->window.app.state.save();
  }), picker.get());
  g_signal_connect(picker->model, "unmap", G_CALLBACK(+[](GtkWidget*, Picker* self) {
    if (!self->finished) { self->finished = true; self->stop(); }
  }), picker.get());
  picker->start();
}
}
void ZaneModels::add_preferences(Window& window, AdwPreferencesGroup* group) {
  add_picker(window, group, false); add_picker(window, group, true);
}
