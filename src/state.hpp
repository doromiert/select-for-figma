#pragma once
#include <string>
#include <vector>

struct SavedTab { std::string url; std::string title; std::string group_id{}; };
struct SavedGroup { std::string id; std::string title; bool collapsed = false; std::string color{}; };
struct Export { std::string path; std::string source; std::string title; std::string date; };
class State {
public:
  explicit State(std::string directory = "");
  static bool migrate_legacy_directory(const std::string& data_home);
  bool restore_session = true;
  bool tab_switcher_mru = false;
  bool notifications = true;
  bool local_fonts = true;
  bool keepassxc_paired = false;
  std::string appearance = "system";
  bool adwaita_editor = true;
  std::string zane_chat_model, zane_chat_effort;
  std::string zane_work_model, zane_work_effort;
  bool zane_account_presence = false;
  bool zane_auto_edit_access = true;
  std::string download_folder;
  int width = 1180;
  int height = 800;
  bool maximized = false;
  std::vector<SavedTab> tabs;
  std::vector<SavedGroup> groups;
  std::vector<SavedTab> recent;
  std::vector<Export> exports;
  void save() const;
  void remember(const SavedTab& tab);
  const std::string& directory() const { return directory_; }
  std::string cache_directory() const;
private:
  std::string directory_;
  void load();
};
