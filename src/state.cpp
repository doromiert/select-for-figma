#include "state.hpp"
#include "policy.hpp"
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <algorithm>
#include <set>

namespace {
std::string text(JsonObject* o, const char* key, const char* fallback = "") {
  if (!json_object_has_member(o, key)) return fallback;
  JsonNode* n = json_object_get_member(o, key);
  return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING ? json_node_get_string(n) : fallback;
}
bool boolean(JsonObject* o, const char* key, bool fallback) {
  if (!json_object_has_member(o, key)) return fallback;
  JsonNode* n = json_object_get_member(o, key);
  return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_BOOLEAN ? json_node_get_boolean(n) : fallback;
}
int integer(JsonObject* o, const char* key, int fallback) {
  if (!json_object_has_member(o, key)) return fallback;
  JsonNode* n = json_object_get_member(o, key);
  return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_INT64 ? json_node_get_int(n) : fallback;
}
void write_tabs(JsonObject* root, const char* key, const std::vector<SavedTab>& tabs) {
  JsonArray* array = json_array_new();
  for (const auto& tab : tabs) {
    if (!Policy::is_file(tab.url)) continue;
    JsonObject* o = json_object_new();
    json_object_set_string_member(o, "url", tab.url.c_str());
    json_object_set_string_member(o, "title", tab.title.c_str());
    if (!tab.group_id.empty()) json_object_set_string_member(o, "group", tab.group_id.c_str());
    json_array_add_object_element(array, o);
  }
  json_object_set_array_member(root, key, array);
}
void read_tabs(JsonObject* root, const char* key, std::vector<SavedTab>& tabs) {
  JsonNode* n = json_object_get_member(root, key);
  if (!n || !JSON_NODE_HOLDS_ARRAY(n)) return;
  JsonArray* array = json_node_get_array(n);
  for (unsigned i = 0; i < std::min(100u, json_array_get_length(array)); ++i) {
    JsonNode* item = json_array_get_element(array, i);
    if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
    JsonObject* o = json_node_get_object(item);
    std::string url = Policy::normalize(text(o, "url"));
    if (Policy::is_file(url)) tabs.push_back({url, text(o, "title", "Untitled File"), text(o, "group")});
  }
}
}

bool State::migrate_legacy_directory(const std::string& data_home) {
  const auto legacy = data_home + "/figma-gtk", destination = data_home + "/select-for-figma";
  if ((g_file_test(destination.c_str(), G_FILE_TEST_EXISTS) || g_file_test(destination.c_str(), G_FILE_TEST_IS_SYMLINK)) || !g_file_test(legacy.c_str(), G_FILE_TEST_IS_DIR)) return true;
  return g_rename(legacy.c_str(), destination.c_str()) == 0;
}

State::State(std::string directory) {
  directory_ = directory.empty() ? std::string(g_get_user_data_dir()) + "/select-for-figma" : std::move(directory);
  const char* downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
  download_folder = downloads ? downloads : g_get_home_dir();
  load();
}
std::string State::cache_directory() const { return directory_ + "/chromium"; }
void State::load() {
  JsonParser* parser = json_parser_new();
  if (!json_parser_load_from_file(parser, (directory_ + "/state.json").c_str(), nullptr)) { g_object_unref(parser); return; }
  JsonNode* node = json_parser_get_root(parser);
  if (!node || !JSON_NODE_HOLDS_OBJECT(node)) { g_object_unref(parser); return; }
  JsonObject* root = json_node_get_object(node);
  restore_session = boolean(root, "restore-session", true);
  tab_switcher_mru = boolean(root, "tab-switcher-mru", false);
  notifications = boolean(root, "notifications", true);
  local_fonts = boolean(root, "local-fonts", true);
  keepassxc_paired = boolean(root, "keepassxc-paired", false);
  appearance = text(root, "appearance", "system");
  adwaita_editor = boolean(root, "adwaita-editor", true);
  const auto old_model = text(root, "zane-model"), old_effort = text(root, "zane-effort");
  zane_chat_model = text(root, "zane-chat-model", old_model.c_str());
  zane_work_model = text(root, "zane-work-model", old_model.c_str());
  zane_chat_effort = text(root, "zane-chat-effort", old_effort.c_str());
  zane_work_effort = text(root, "zane-work-effort", old_effort.c_str());
  zane_account_presence = boolean(root, "zane-account-presence", false);
  zane_auto_edit_access = boolean(root, "zane-auto-edit-access", true);
  for (auto* value : {&zane_chat_model, &zane_work_model}) if (value->size() > 200) value->clear();
  for (auto* value : {&zane_chat_effort, &zane_work_effort}) if (value->size() > 32) value->clear();
  if (appearance != "light" && appearance != "dark") appearance = "system";
  download_folder = text(root, "download-folder", download_folder.c_str());
  width = std::clamp(integer(root, "width", 1180), 360, 7680);
  height = std::clamp(integer(root, "height", 800), 294, 4320);
  maximized = boolean(root, "maximized", false);
  if (restore_session) read_tabs(root, "tabs", tabs);
  JsonNode* group_node = json_object_get_member(root, "groups");
  std::set<std::string> ids;
  if (group_node && JSON_NODE_HOLDS_ARRAY(group_node)) {
    JsonArray* array = json_node_get_array(group_node);
    for (unsigned i = 0; i < std::min(100u, json_array_get_length(array)); ++i) {
      JsonNode* item = json_array_get_element(array, i);
      if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
      JsonObject* group = json_node_get_object(item);
      const auto id = text(group, "id"), title = text(group, "title");
      if (!id.empty() && id.size() <= 64 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos &&
          !title.empty() && title.size() <= 384 && ids.insert(id).second)
      {
        auto color = text(group,"color");
        if (color.size() != 7 || color[0] != '#' || color.find_first_not_of("0123456789abcdefABCDEF",1) != std::string::npos) color.clear();
        groups.push_back({id,title,boolean(group,"collapsed",false),color});
      }
    }
  }
  for (auto& tab : tabs) if (!ids.count(tab.group_id)) tab.group_id.clear();
  groups.erase(std::remove_if(groups.begin(),groups.end(),[&](const auto& group) {
    return std::none_of(tabs.begin(),tabs.end(),[&](const auto& tab) { return tab.group_id == group.id; });
  }),groups.end());
  read_tabs(root, "recent", recent);
  JsonNode* e = json_object_get_member(root, "exports");
  if (e && JSON_NODE_HOLDS_ARRAY(e)) {
    JsonArray* array = json_node_get_array(e);
    for (unsigned i = 0; i < std::min(300u, json_array_get_length(array)); ++i) {
      JsonNode* item = json_array_get_element(array, i);
      if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
      JsonObject* o = json_node_get_object(item);
      std::string path = text(o, "path");
      if (g_path_is_absolute(path.c_str())) exports.push_back({path, text(o, "source"), text(o, "title"), text(o, "date")});
    }
  }
  g_object_unref(parser);
}
void State::remember(const SavedTab& tab) {
  if (!Policy::is_file(tab.url)) return;
  const auto key = Policy::file_key(tab.url);
  recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const auto& t) { return Policy::file_key(t.url) == key; }), recent.end());
  recent.insert(recent.begin(), tab);
  if (recent.size() > 20) recent.resize(20);
}
void State::save() const {
  if (g_mkdir_with_parents(directory_.c_str(), 0700) != 0) { g_warning("Could not create app data directory"); return; }
  JsonObject* root = json_object_new();
  json_object_set_int_member(root, "version", 1);
  json_object_set_boolean_member(root, "restore-session", restore_session);
  json_object_set_boolean_member(root, "tab-switcher-mru", tab_switcher_mru);
  json_object_set_boolean_member(root, "notifications", notifications);
  json_object_set_boolean_member(root, "local-fonts", local_fonts);
  json_object_set_boolean_member(root, "keepassxc-paired", keepassxc_paired);
  json_object_set_string_member(root, "zane-chat-model", zane_chat_model.c_str());
  json_object_set_string_member(root, "zane-chat-effort", zane_chat_effort.c_str());
  json_object_set_string_member(root, "zane-work-model", zane_work_model.c_str());
  json_object_set_string_member(root, "zane-work-effort", zane_work_effort.c_str());
  json_object_set_boolean_member(root, "zane-account-presence", zane_account_presence);
  json_object_set_boolean_member(root, "zane-auto-edit-access", zane_auto_edit_access);
  json_object_set_string_member(root, "appearance", appearance.c_str());
  json_object_set_boolean_member(root, "adwaita-editor", adwaita_editor);
  json_object_set_string_member(root, "download-folder", download_folder.c_str());
  json_object_set_int_member(root, "width", width);
  json_object_set_int_member(root, "height", height);
  json_object_set_boolean_member(root, "maximized", maximized);
  write_tabs(root, "tabs", restore_session ? tabs : std::vector<SavedTab>{});
  write_tabs(root, "recent", recent);
  JsonArray* group_array = json_array_new();
  for (const auto& group : groups) {
    JsonObject* item = json_object_new();
    json_object_set_string_member(item, "id", group.id.c_str());
    json_object_set_string_member(item, "title", group.title.c_str());
    json_object_set_boolean_member(item, "collapsed", group.collapsed);
    if (!group.color.empty()) json_object_set_string_member(item,"color",group.color.c_str());
    json_array_add_object_element(group_array, item);
  }
  json_object_set_array_member(root, "groups", group_array);
  JsonArray* array = json_array_new();
  for (const auto& e : exports) {
    JsonObject* o = json_object_new();
    json_object_set_string_member(o, "path", e.path.c_str());
    json_object_set_string_member(o, "source", e.source.c_str());
    json_object_set_string_member(o, "title", e.title.c_str());
    json_object_set_string_member(o, "date", e.date.c_str());
    json_array_add_object_element(array, o);
  }
  json_object_set_array_member(root, "exports", array);
  JsonNode* node = json_node_new(JSON_NODE_OBJECT); json_node_take_object(node, root);
  JsonGenerator* generator = json_generator_new(); json_generator_set_root(generator, node);
  json_generator_set_pretty(generator, true);
  gsize length; char* data = json_generator_to_data(generator, &length);
  GError* error = nullptr;
  if (!g_file_set_contents_full((directory_ + "/state.json").c_str(), data, length,
      static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE), 0600, &error)) {
    g_warning("Could not save app state: %s", error->message); g_error_free(error);
  }
  g_free(data); g_object_unref(generator); json_node_free(node);
}
