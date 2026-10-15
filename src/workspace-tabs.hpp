#pragma once
#include <adwaita.h>
#include "browser.hpp"
#include "state.hpp"
#include <map>

class Window;
class WorkspaceTabs {
public:
  explicit WorkspaceTabs(Window& window);
  ~WorkspaceTabs();
  static GtkWidget* thumbnail(CefRefPtr<Browser> browser, int width, int height);
  GtkWidget* strip() const { return strip_; }
  GtkWidget* overview() const { return overview_; }
  GtkWidget* create_root(GtkWidget* workspace);
  GtkWidget* create_pages(GtkWidget* content);
  double page_transition_offset() const;
  void update();
  void show_overview(bool visible);
  bool overview_open() const { return overview_open_; }
  void activate(CefRefPtr<Browser> browser);
  void new_tab();
  std::string create_group(const std::string& title, CefRefPtr<Browser> browser, const std::string& color = "");
  void assign(CefRefPtr<Browser> browser, const std::string& id);
  void toggle_group(const std::string& id);
  void name_dialog(CefRefPtr<Browser> browser, const std::string& id = "");
  void ungroup(const std::string& id);
  void color_dialog(const std::string& id);
  void expand_overview(const std::string& id);
  GtkWidget* group_button(const std::string& id) const;
  GtkWidget* preview_button(Browser* browser) const;
  double transition_progress() const;
  GtkWidget* transition_origin() const;
  double drop_progress() const;
  GtkWidget* strip_tab(Browser* browser) const;
  void begin_drag(GtkWidget* tile);
  void hover_drag(GtkWidget* tile);
  void end_drag();
  GtkWidget* overview_group(const std::string& id) const;
  GtkWidget* group_drawer() const { return drawer_; }
  GtkWidget* home_card() const { return home_card_; }
  GtkWidget* new_tab_button() const { return new_tab_button_; }
private:
  Window& win_;
  GtkWidget* pages_host_ = nullptr;
  GtkWidget* strip_;
  GtkWidget* strip_scroll_;
  GtkWidget* scroll_back_;
  GtkWidget* scroll_forward_;
  GtkWidget* header_collapse_ = nullptr;
  AdwAnimation* scroll_animation_ = nullptr;
  guint scroll_refresh_ = 0;
  GtkWidget* strip_contents_;
  GtkWidget* overview_;
  GtkWidget* overview_contents_;
  GtkWidget* root_ = nullptr;
  GtkWidget* search_bar_;
  AdwWindowTitle* overview_title_;
  GtkWidget* drawer_ = nullptr;
  GtkWidget* home_card_ = nullptr;
  GtkWidget* new_tab_button_ = nullptr;
  std::vector<GtkWidget*> overview_tiles_;
  std::map<Browser*, GtkWidget*> previews_, preview_buttons_;
  std::map<Browser*, GtkWidget*> strip_tabs_;
  CefRefPtr<Browser> pending_activation_;
  bool selecting_ = false, normalizing_ = false;
  bool zoom_from_new_tab_ = false;
  bool drag_ungroup_ = false;
  bool drag_in_overview_ = false, dropping_in_overview_ = false, settling_ = false;
  CefRefPtr<Browser> dropping_browser_;
  GdkPaintable* drag_image_ = nullptr;
  int drag_hotspot_x_ = 0, drag_hotspot_y_ = 0;
  struct DragPosition { GtkWidget* tile; graphene_point_t point; };
  std::vector<DragPosition> drag_positions_;
  GtkWidget* drag_tile_ = nullptr;
  GtkWidget* drag_hover_ = nullptr;
  GtkWidget* drag_merge_ = nullptr;
  GtkCssProvider* drag_style_;
  GtkCssProvider* group_style_;
  std::string header_name_, header_group_;
  GdkDisplay* drag_display_;
  std::map<std::string, GtkWidget*> overview_groups_;
  int tile_width_ = 220, columns_ = 1, layout_width_ = 0;
  std::string signature_, expanded_, drawer_id_, query_;
  bool overview_open_ = false;
  std::map<std::string, GtkWidget*> group_buttons_, strip_groups_;
  std::string drag_group_, dropping_group_;
  std::map<std::string, Browser*> last_group_tabs_;
  Browser* last_current_ = nullptr;
  std::string last_current_group_;
  SavedGroup* group(const std::string& id);
  GdkRGBA group_color(const std::string& id);
  void update_colors(const std::string& header_group);
  void expand_header(const std::string& id);
  void select_group(const std::string& id);
  std::vector<CefRefPtr<Browser>> pages() const;
  void changed();
  void normalize_order();
  void rebuild_strip();
  void scroll_strip(int direction);
  void rebuild_overview();
  void dim_overview();
  GtkWidget* tab(CefRefPtr<Browser> browser, bool preview);
  void tab_menu(GtkWidget* parent, CefRefPtr<Browser> browser, double x, double y);
  void group_drop(GtkWidget* widget, const std::string& id);
  void attach_drag(GtkWidget* source, GtkWidget* tile, CefRefPtr<Browser> browser = nullptr, const std::string& id = "");
  bool drop_on(const GValue* value, GtkWidget* widget, double x, double y, CefRefPtr<Browser> destination, const std::string& target_id);
  void attach_reorder_drop(GtkWidget* widget);
  void attach_window_drop(GtkWidget* widget);
  void detach(CefRefPtr<Browser> browser, const std::string& id);
  void receive(CefRefPtr<Browser> browser, const std::string& id, Window& origin, int position);
  void prepare_drop(CefRefPtr<Browser> browser, GtkWidget* widget, double x, double y, const std::string& id = "");
  void group_menu(GtkWidget* parent, const std::string& id, double x, double y);
  void popup(GtkWidget* parent, GMenu* menu, double x, double y);
};
