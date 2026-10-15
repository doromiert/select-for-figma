#include "state.hpp"
#include <glib.h>
#include <filesystem>

int main(int argc, char** argv) {
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/state/round-trip", +[] {
    char* directory = g_dir_make_tmp("figma-gtk-state-XXXXXX", nullptr);
    {
      State state(directory); state.appearance = "dark"; state.zane_chat_model = "example-fast"; state.zane_chat_effort = "low";
      state.zane_work_model = "example-smart"; state.zane_work_effort = "high";
      g_assert_false(state.zane_account_presence); state.zane_account_presence = true;
      g_assert_true(state.zane_auto_edit_access); state.zane_auto_edit_access=false;
      state.tabs = {{"https://www.figma.com/design/a/File", "File"}, {"https://www.figma.com/app_auth/secret/grant", "Sign in"}, {"https://example.com/", "Untrusted"}};
      state.remember({"https://www.figma.com/design/a/File?node-id=1", "New title"});
      state.remember({"https://www.figma.com/design/a/File?node-id=2", "Newest"});
      state.save();
      State loaded(directory);
      g_assert_cmpuint(loaded.tabs.size(), ==, 1); g_assert_cmpuint(loaded.recent.size(), ==, 1);
      g_assert_cmpstr(loaded.recent[0].title.c_str(), ==, "Newest"); g_assert_cmpstr(loaded.appearance.c_str(), ==, "dark");
      g_assert_cmpstr(loaded.zane_chat_model.c_str(), ==, "example-fast"); g_assert_cmpstr(loaded.zane_chat_effort.c_str(), ==, "low");
      g_assert_cmpstr(loaded.zane_work_model.c_str(), ==, "example-smart"); g_assert_cmpstr(loaded.zane_work_effort.c_str(), ==, "high");
      g_assert_true(loaded.zane_account_presence);
      g_assert_false(loaded.zane_auto_edit_access);
      loaded.restore_session = false; loaded.save();
      State disabled(directory); g_assert_true(disabled.tabs.empty()); g_assert_cmpuint(disabled.recent.size(), ==, 1);
    }
    std::filesystem::remove_all(directory); g_free(directory);
  });
  g_test_add_func("/state/malformed", +[] {
    char* directory = g_dir_make_tmp("figma-gtk-state-XXXXXX", nullptr);
    const std::string file = std::string(directory) + "/state.json";
    g_file_set_contents(file.c_str(), "{\"width\":-5,\"appearance\":42,\"tabs\":\"wrong type\",\"recent\":[null,5,{}],\"exports\":[false]}", -1, nullptr);
    State state(directory); g_assert_cmpint(state.width, ==, 360); g_assert_true(state.tabs.empty()); g_assert_true(state.recent.empty());
    std::filesystem::remove_all(directory); g_free(directory);
  });
  g_test_add_func("/state/tab-groups", +[] {
    char* directory = g_dir_make_tmp("select-groups-state-XXXXXX", nullptr);
    State state(directory); state.groups = {{"work-1", "Work", true, "#e66100"}, {"personal", "Personal", false}, {"ghost", "Empty group", true}};
    state.tabs = {{"https://www.figma.com/design/a/File", "A", "work-1"}, {"https://www.figma.com/design/b/File", "B", "personal"}, {"https://www.figma.com/design/c/File", "C", "removed"}};
    state.save(); State loaded(directory);
    g_assert_cmpuint(loaded.groups.size(), ==, 2); g_assert_true(loaded.groups[0].collapsed); g_assert_false(loaded.groups[1].collapsed);
    g_assert_cmpstr(loaded.groups[0].color.c_str(), ==, "#e66100"); g_assert_true(loaded.groups[1].color.empty());
    g_assert_cmpstr(loaded.groups[0].title.c_str(), ==, "Work"); g_assert_cmpstr(loaded.tabs[0].group_id.c_str(), ==, "work-1");
    g_assert_cmpstr(loaded.tabs[1].group_id.c_str(), ==, "personal"); g_assert_true(loaded.tabs[2].group_id.empty());
    loaded.restore_session = false; loaded.save(); State disabled(directory); g_assert_true(disabled.tabs.empty()); g_assert_true(disabled.groups.empty());
    const std::string file = std::string(directory)+"/state.json";
    g_file_set_contents(file.c_str(), R"({"groups":[null,{}, {"id":"bad id","title":"Bad"}, {"id":"valid","title":"Good","collapsed":true,"color":"bad color"}, {"id":"valid","title":"Duplicate"}],"tabs":[{"url":"https://www.figma.com/design/a/File","title":"A","group":"bad id"},{"url":"https://www.figma.com/design/b/File","title":"B","group":"valid"}]})", -1, nullptr);
    State malformed(directory); g_assert_cmpuint(malformed.groups.size(), ==, 1); g_assert_true(malformed.groups[0].color.empty()); g_assert_true(malformed.tabs[0].group_id.empty());
    std::filesystem::remove_all(directory); g_free(directory);
  });
  return g_test_run();
}
