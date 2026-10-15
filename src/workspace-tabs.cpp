#include "workspace-tabs.hpp"
#include "application.hpp"
#include "policy.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <cstring>
#include <cmath>

namespace {
struct SelectTabButton { GtkButton parent; };
struct SelectTabButtonClass { GtkButtonClass parent; };
G_DEFINE_TYPE(SelectTabButton, select_tab_button, GTK_TYPE_BUTTON)
void select_tab_button_class_init(SelectTabButtonClass* klass) {
  gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(klass), GTK_ACCESSIBLE_ROLE_TAB);
}
void select_tab_button_init(SelectTabButton*) {}

struct SelectPageSlide {
  GtkWidget parent;
  GtkWidget* child;
  AdwAnimation* animation;
  GskRenderNode *frame, *outgoing;
  double progress;
  int direction, width, height;
};
struct SelectPageSlideClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(SelectPageSlide, select_page_slide, GTK_TYPE_WIDGET)
void page_slide_finish(SelectPageSlide* slide) {
  g_clear_pointer(&slide->outgoing, gsk_render_node_unref);
  slide->progress = 1;
  gtk_widget_set_can_target(slide->child, true);
  gtk_widget_queue_draw(GTK_WIDGET(slide));
}
void page_slide_start(SelectPageSlide* slide, int direction) {
  adw_animation_reset(slide->animation);
  g_clear_pointer(&slide->outgoing, gsk_render_node_unref);
  if (!direction || !slide->frame || !gtk_widget_get_mapped(GTK_WIDGET(slide))) { page_slide_finish(slide); return; }
  // The last composited frame also preserves the visible position on interruption.
  slide->outgoing = gsk_render_node_ref(slide->frame);
  slide->direction = direction;
  slide->progress = 0;
  gtk_widget_set_can_target(slide->child, false);
  adw_animation_play(slide->animation);
}
void page_slide_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto* slide = reinterpret_cast<SelectPageSlide*>(widget);
  auto* content = gtk_snapshot_new();
  const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(slide->width), static_cast<float>(slide->height));
  gtk_snapshot_push_clip(content, &bounds);
  if (slide->outgoing) {
    const graphene_point_t old_position = GRAPHENE_POINT_INIT(static_cast<float>(-slide->direction*slide->progress*slide->width), 0);
    gtk_snapshot_save(content); gtk_snapshot_translate(content, &old_position);
    gtk_snapshot_append_node(content, slide->outgoing); gtk_snapshot_restore(content);
    const graphene_point_t new_position = GRAPHENE_POINT_INIT(static_cast<float>(slide->direction*(1-slide->progress)*slide->width), 0);
    gtk_snapshot_translate(content, &new_position);
  }
  gtk_widget_snapshot_child(widget, slide->child, content);
  gtk_snapshot_pop(content);
  g_clear_pointer(&slide->frame, gsk_render_node_unref);
  slide->frame = gtk_snapshot_free_to_node(content);
  if (slide->frame) gtk_snapshot_append_node(snapshot, slide->frame);
}
void select_page_slide_class_init(SelectPageSlideClass* klass) {
  auto* widget = GTK_WIDGET_CLASS(klass);
  widget->measure = +[](GtkWidget* widget, GtkOrientation orientation, int size, int* minimum, int* natural, int* minbase, int* natbase) {
    gtk_widget_measure(reinterpret_cast<SelectPageSlide*>(widget)->child, orientation, size, minimum, natural, minbase, natbase);
  };
  widget->size_allocate = +[](GtkWidget* widget, int width, int height, int baseline) {
    auto* slide = reinterpret_cast<SelectPageSlide*>(widget);
    if (slide->width != width || slide->height != height) {
      adw_animation_skip(slide->animation);
      g_clear_pointer(&slide->frame, gsk_render_node_unref);
      slide->width = width; slide->height = height;
    }
    gtk_widget_allocate(slide->child, width, height, baseline, nullptr);
  };
  widget->snapshot = page_slide_snapshot;
  gtk_widget_class_set_css_name(widget, "selectpageslide");
  G_OBJECT_CLASS(klass)->dispose = +[](GObject* object) {
    auto* slide = reinterpret_cast<SelectPageSlide*>(object);
    g_clear_object(&slide->animation);
    g_clear_pointer(&slide->frame, gsk_render_node_unref);
    g_clear_pointer(&slide->outgoing, gsk_render_node_unref);
    if (slide->child) { gtk_widget_unparent(slide->child); slide->child = nullptr; }
    G_OBJECT_CLASS(select_page_slide_parent_class)->dispose(object);
  };
}
void select_page_slide_init(SelectPageSlide* slide) {
  slide->progress = 1;
  auto* target = adw_callback_animation_target_new(+[](double progress, gpointer data) {
    auto* slide = static_cast<SelectPageSlide*>(data); slide->progress = progress; gtk_widget_queue_draw(GTK_WIDGET(slide));
  }, slide, nullptr);
  slide->animation = adw_timed_animation_new(GTK_WIDGET(slide), 0, 1, 250, target);
  adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(slide->animation), ADW_EASE_OUT_CUBIC);
  g_signal_connect(slide->animation, "done", G_CALLBACK(+[](AdwAnimation*, SelectPageSlide* slide) { page_slide_finish(slide); }), slide);
}

struct StripLayout {
  std::map<std::string, graphene_rect_t> current, from, to;
  AdwAnimation* animation = nullptr;
  double progress = 1;
  int count = -1;
  std::string shape;
};
struct SelectTabStrip { GtkWidget parent; StripLayout* layout; };
struct SelectTabStripClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(SelectTabStrip, select_tab_strip, GTK_TYPE_WIDGET)
std::string strip_key(GtkWidget* child) {
  auto* key = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "layout-key"));
  return key ? key : "";
}
void strip_measure(GtkWidget* widget, GtkOrientation orientation, int size, int* minimum, int* natural, int* minbase, int* natbase) {
  *minimum = *natural = 0; *minbase = *natbase = -1;
  int count = 0;
  for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
    int min, nat; gtk_widget_measure(child, orientation, size, &min, &nat, nullptr, nullptr);
    *minimum = std::max(*minimum, min); *natural = std::max(*natural, nat);
    ++count;
  }
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    *minimum = std::max(180,*minimum)*count+std::max(0,count-1)*6;
    *natural = std::max(*minimum,*natural*count+std::max(0,count-1)*6);
  }
}
void strip_allocate(GtkWidget* widget, int width, int height, int baseline) {
  auto& layout = *reinterpret_cast<SelectTabStrip*>(widget)->layout;
  std::vector<GtkWidget*> children;
  for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
    children.push_back(child);
  }
  if (children.empty()) return;
  const int available = std::max(0, width-static_cast<int>(children.size()-1)*6);
  std::map<std::string, graphene_rect_t> target;
  int x = 0;
  for (size_t i = 0; i < children.size(); ++i) {
    auto* child = children[i];
    const int child_width = available*(i+1)/children.size()-available*i/children.size();
    int child_min, child_nat; gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, child_width, &child_min, &child_nat, nullptr, nullptr);
    const int child_height = std::min(height, child_nat);
    target[strip_key(child)] = GRAPHENE_RECT_INIT(static_cast<float>(x), static_cast<float>((height-child_height)/2), static_cast<float>(child_width), static_cast<float>(child_height));
    x += child_width+6;
  }
  const int count = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tab-count"));
  const auto* shape_value = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "tab-shape"));
  const std::string shape = shape_value ? shape_value : "";
  const bool structure_changed = layout.count >= 0 && (count != layout.count || shape != layout.shape);
  const bool dropping = g_object_get_data(G_OBJECT(widget), "drop-layout") != nullptr;
  g_object_set_data(G_OBJECT(widget), "drop-layout", nullptr);
  layout.count = count; layout.shape = shape;
  if (dropping) { adw_animation_skip(layout.animation); layout.progress = 1; layout.to = target; }
  else if (structure_changed && !layout.current.empty() && gtk_widget_get_mapped(widget)) {
    // Preserve current allocations across strip rebuilds. Allocate actual
    // widgets at each width, so text stays sharp and does not stretch.
    layout.from = layout.current; layout.to = target;
    for (const auto& [key, rect] : target) if (!layout.from.contains(key)) {
      auto start = rect; start.origin.x += start.size.width/2; start.size.width = 0; layout.from[key] = start;
    }
    layout.progress = 0; adw_animation_play(layout.animation);
  } else if (adw_animation_get_state(layout.animation) == ADW_ANIMATION_PLAYING) {
    // Changes during a running resize continue from the visible allocation.
    bool changed = target.size() != layout.to.size();
    if (!changed) for (const auto& [key, rect] : target) {
      const auto found = layout.to.find(key);
      if (found == layout.to.end() || !graphene_rect_equal(&rect, &found->second)) { changed = true; break; }
    }
    if (changed) { layout.from = layout.current; layout.to = target; layout.progress = 0; adw_animation_play(layout.animation); }
  } else { layout.to = target; layout.progress = 1; }
  layout.current.clear();
  for (auto* child : children) {
    const auto key = strip_key(child); auto rect = target.at(key);
    if (layout.progress < 1 && layout.from.contains(key) && layout.to.contains(key)) graphene_rect_interpolate(&layout.from.at(key), &layout.to.at(key), layout.progress, &rect);
    layout.current[key] = rect;
    int min, nat; gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, height, &min, &nat, nullptr, nullptr);
    graphene_point_t point = rect.origin;
    gtk_widget_allocate(child, std::max(min, static_cast<int>(std::round(rect.size.width))), rect.size.height, baseline, gsk_transform_translate(nullptr, &point));
  }
}
void strip_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto& layout = *reinterpret_cast<SelectTabStrip*>(widget)->layout;
  for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
    const auto found = layout.current.find(strip_key(child));
    if (found == layout.current.end()) continue;
    // New tabs can grow from zero without allocating below GTK's minimum.
    const bool clipped = found->second.size.width < gtk_widget_get_width(child);
    if (clipped) gtk_snapshot_push_clip(snapshot, &found->second);
    gtk_widget_snapshot_child(widget, child, snapshot);
    if (clipped) gtk_snapshot_pop(snapshot);
  }
}
void strip_dispose(GObject* object) {
  auto* strip = reinterpret_cast<SelectTabStrip*>(object);
  g_clear_object(&strip->layout->animation);
  while (auto* child = gtk_widget_get_first_child(GTK_WIDGET(strip))) gtk_widget_unparent(child);
  G_OBJECT_CLASS(select_tab_strip_parent_class)->dispose(object);
}
void strip_finalize(GObject* object) {
  delete reinterpret_cast<SelectTabStrip*>(object)->layout;
  G_OBJECT_CLASS(select_tab_strip_parent_class)->finalize(object);
}
void select_tab_strip_class_init(SelectTabStripClass* klass) {
  auto* widget = GTK_WIDGET_CLASS(klass); widget->measure = strip_measure; widget->size_allocate = strip_allocate; widget->snapshot = strip_snapshot;
  gtk_widget_class_set_css_name(widget, "box"); gtk_widget_class_set_accessible_role(widget, GTK_ACCESSIBLE_ROLE_TAB_LIST);
  G_OBJECT_CLASS(klass)->dispose = strip_dispose; G_OBJECT_CLASS(klass)->finalize = strip_finalize;
}
void select_tab_strip_init(SelectTabStrip* strip) {
  strip->layout = new StripLayout;
  auto* target = adw_callback_animation_target_new(+[](double progress, gpointer data) {
    auto* strip = static_cast<SelectTabStrip*>(data); strip->layout->progress = progress; gtk_widget_queue_allocate(GTK_WIDGET(strip));
  }, strip, nullptr);
  strip->layout->animation = adw_timed_animation_new(GTK_WIDGET(strip), 0, 1, 250, target);
  adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(strip->layout->animation), ADW_EASE_OUT_CUBIC);
}
// Fixed logical thumbnail sizes prevent GtkPicture's height-for-width
// natural request from stretching centered rows as a drawer opens.
struct SelectPreviewPicture { GtkWidget parent; GdkPaintable* image; int width, height; };
struct SelectPreviewPictureClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(SelectPreviewPicture, select_preview_picture, GTK_TYPE_WIDGET)
void preview_measure(GtkWidget* widget, GtkOrientation orientation, int, int* minimum, int* natural, int* minbase, int* natbase) {
  auto* picture = reinterpret_cast<SelectPreviewPicture*>(widget);
  *minimum = *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? picture->width : picture->height; *minbase = *natbase = -1;
}
void preview_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto* picture = reinterpret_cast<SelectPreviewPicture*>(widget);
  gdk_paintable_snapshot(picture->image,snapshot,gtk_widget_get_width(widget),gtk_widget_get_height(widget));
}
void preview_dispose(GObject* object) {
  g_clear_object(&reinterpret_cast<SelectPreviewPicture*>(object)->image); G_OBJECT_CLASS(select_preview_picture_parent_class)->dispose(object);
}
void select_preview_picture_class_init(SelectPreviewPictureClass* klass) {
  auto* widget = GTK_WIDGET_CLASS(klass); widget->measure = preview_measure; widget->snapshot = preview_snapshot;
  widget->get_request_mode = +[](GtkWidget*) { return GTK_SIZE_REQUEST_CONSTANT_SIZE; };
  gtk_widget_class_set_css_name(widget,"picture"); gtk_widget_class_set_accessible_role(widget,GTK_ACCESSIBLE_ROLE_IMG);
  G_OBJECT_CLASS(klass)->dispose = preview_dispose;
}
void select_preview_picture_init(SelectPreviewPicture*) {}
GtkWidget* preview_picture(GdkPaintable* image, int width, int height) {
  auto* widget = GTK_WIDGET(g_object_new(select_preview_picture_get_type(),nullptr)); auto* picture = reinterpret_cast<SelectPreviewPicture*>(widget);
  picture->image = GDK_PAINTABLE(g_object_ref(image)); picture->width = width; picture->height = height; return widget;
}
// Reveal a drawer by growing its clip from the top. GtkRevealer's slide
// translates the full child, exposing its bottom before its heading.
struct SelectGroupDrawer {
  GtkWidget parent; GtkWidget* child; AdwAnimation* animation;
  double progress; gboolean open; guint start_tick;
};
struct SelectGroupDrawerClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(SelectGroupDrawer, select_group_drawer, GTK_TYPE_WIDGET)
void drawer_measure(GtkWidget* widget, GtkOrientation orientation, int size, int* minimum, int* natural, int* minbase, int* natbase) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget);
  gtk_widget_measure(drawer->child, orientation, size, minimum, natural, nullptr, nullptr);
  if (orientation == GTK_ORIENTATION_VERTICAL) { *minimum = std::ceil(*minimum*drawer->progress); *natural = std::ceil(*natural*drawer->progress); }
  *minbase = *natbase = -1;
}
void drawer_allocate(GtkWidget* widget, int width, int, int) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget); int min, nat;
  gtk_widget_measure(drawer->child, GTK_ORIENTATION_VERTICAL, width, &min, &nat, nullptr, nullptr);
  gtk_widget_allocate(drawer->child, width, nat, -1, nullptr);
}
void drawer_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget);
  if (drawer->progress <= 0 || !gtk_widget_get_child_visible(drawer->child)) return;
  graphene_rect_t clip = GRAPHENE_RECT_INIT(0,0,static_cast<float>(gtk_widget_get_width(widget)),static_cast<float>(gtk_widget_get_height(widget)));
  GskRoundedRect rounded; gsk_rounded_rect_init_from_rect(&rounded,&clip,std::min(12.f,clip.size.height/2));
  gtk_snapshot_push_rounded_clip(snapshot, &rounded); gtk_widget_snapshot_child(widget, drawer->child, snapshot); gtk_snapshot_pop(snapshot);
}
void drawer_dispose(GObject* object) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(object);
  if (drawer->start_tick) { gtk_widget_remove_tick_callback(GTK_WIDGET(drawer), drawer->start_tick); drawer->start_tick = 0; }
  g_clear_object(&drawer->animation);
  if (drawer->child) { gtk_widget_unparent(drawer->child); drawer->child = nullptr; }
  G_OBJECT_CLASS(select_group_drawer_parent_class)->dispose(object);
}
void select_group_drawer_class_init(SelectGroupDrawerClass* klass) {
  auto* widget = GTK_WIDGET_CLASS(klass); widget->measure = drawer_measure; widget->size_allocate = drawer_allocate; widget->snapshot = drawer_snapshot;
  gtk_widget_class_set_css_name(widget, "groupdrawer"); gtk_widget_class_set_accessible_role(widget, GTK_ACCESSIBLE_ROLE_GROUP);
  G_OBJECT_CLASS(klass)->dispose = drawer_dispose;
}
void select_group_drawer_init(SelectGroupDrawer* drawer) {
  auto* target = adw_callback_animation_target_new(+[](double progress, gpointer p) {
    auto* drawer = static_cast<SelectGroupDrawer*>(p); drawer->progress = progress; gtk_widget_queue_resize(GTK_WIDGET(drawer));
  }, drawer, nullptr);
  drawer->animation = adw_timed_animation_new(GTK_WIDGET(drawer),0,1,250,target);
  adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(drawer->animation), ADW_EASE_OUT_CUBIC);
  g_signal_connect(drawer->animation, "done", G_CALLBACK(+[](AdwAnimation*, SelectGroupDrawer* drawer) {
    if (!drawer->open && drawer->progress <= 0) gtk_widget_set_child_visible(drawer->child, false);
  }), drawer);
  gtk_widget_set_vexpand(GTK_WIDGET(drawer), false); gtk_widget_set_valign(GTK_WIDGET(drawer), GTK_ALIGN_START);
}
void drawer_animate(GtkWidget* widget) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget); const double goal = drawer->open ? 1 : 0;
  if (drawer->progress == goal) { if (!drawer->open) gtk_widget_set_child_visible(drawer->child,false); return; }
  adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(drawer->animation), drawer->progress);
  adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(drawer->animation), goal);
  // Resume the remaining cubic curve when title/preview updates rebuild it.
  adw_timed_animation_set_duration(ADW_TIMED_ANIMATION(drawer->animation), std::max(1,static_cast<int>(250*std::cbrt(std::abs(goal-drawer->progress)))));
  adw_animation_play(drawer->animation);
}
void drawer_set_open(GtkWidget* widget, bool open) {
  auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget); drawer->open = open;
  if (open) gtk_widget_set_child_visible(drawer->child, true);
  if (gtk_widget_get_mapped(widget)) drawer_animate(widget);
  else if (!drawer->start_tick) drawer->start_tick = gtk_widget_add_tick_callback(widget,+[](GtkWidget* widget, GdkFrameClock*, gpointer) -> gboolean {
    reinterpret_cast<SelectGroupDrawer*>(widget)->start_tick = 0; drawer_animate(widget); return G_SOURCE_REMOVE;
  },nullptr,nullptr);
}
GtkWidget* drawer_new(GtkWidget* child, double progress) {
  auto* widget = GTK_WIDGET(g_object_new(select_group_drawer_get_type(),nullptr)); auto* drawer = reinterpret_cast<SelectGroupDrawer*>(widget);
  drawer->child = child; drawer->progress = progress; gtk_widget_set_parent(child,widget); gtk_widget_set_child_visible(child,progress > 0); return widget;
}
// The bounds interpolation, 400 ms ADW_EASE curve and rounded clip follow
// libadwaita's AdwTabOverview (LGPL-2.1-or-later), adapted for grouped cards.
// Original: Copyright (C) 2021-2022 Purism SPC; author Alice Mikhaylenko.
// https://gitlab.gnome.org/GNOME/libadwaita/-/blob/main/src/adw-tab-overview.c
struct SelectOverviewHost {
  GtkWidget parent;
  GtkWidget *workspace, *overview, *thumbnail, *page;
  AdwAnimation* animation;
  AdwAnimation* drop_animation; GdkPaintable* drop_image; GtkWidget* drop_target;
  graphene_rect_t drop_from, drop_to; double drop_progress;
  GdkPaintable* activation_image;
  double progress;
  gboolean open, animating, group_target;
  int width; guint resize_idle;
};
struct SelectOverviewHostClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(SelectOverviewHost, select_overview_host, GTK_TYPE_WIDGET)
void host_measure(GtkWidget* widget, GtkOrientation orientation, int size, int* minimum, int* natural, int* minbase, int* natbase) {
  auto* host = reinterpret_cast<SelectOverviewHost*>(widget);
  gtk_widget_measure(host->workspace, orientation, size, minimum, natural, minbase, natbase);
}
void host_allocate(GtkWidget* widget, int width, int height, int baseline) {
  auto* host = reinterpret_cast<SelectOverviewHost*>(widget);
  gtk_widget_allocate(host->workspace, width, height, baseline, nullptr);
  if (gtk_widget_get_child_visible(host->overview)) {
    int min, nat;
    gtk_widget_measure(host->overview, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, nullptr, nullptr);
    gtk_widget_measure(host->overview, GTK_ORIENTATION_VERTICAL, width, &min, &nat, nullptr, nullptr);
    gtk_widget_allocate(host->overview, width, height, baseline, nullptr);
  }
  if (host->width != width) {
    host->width = width;
    if (!host->resize_idle) host->resize_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer data) -> gboolean {
      auto* host = static_cast<SelectOverviewHost*>(data); host->resize_idle = 0;
      if (host->open) g_signal_emit_by_name(host, "layout-changed", host->width);
      return G_SOURCE_REMOVE;
    }, host, nullptr);
  }
}
void host_snapshot_content(GtkWidget* widget, GtkSnapshot* snapshot) {
  auto* host = reinterpret_cast<SelectOverviewHost*>(widget);
  if (!host->animating) {
    gtk_widget_snapshot_child(widget, host->open ? host->overview : host->workspace, snapshot);
    return;
  }
  const double p = host->progress;
  graphene_rect_t view, thumb;
  const bool zoom = host->page && host->thumbnail &&
    gtk_widget_compute_bounds(host->page, widget, &view) &&
    gtk_widget_compute_bounds(host->thumbnail, widget, &thumb) &&
    view.size.width > 0 && view.size.height > 0 && thumb.size.width > 0 && thumb.size.height > 0;
  if (!zoom) {
    gtk_snapshot_push_cross_fade(snapshot, p);
    gtk_widget_snapshot_child(widget, host->workspace, snapshot); gtk_snapshot_pop(snapshot);
    gtk_widget_snapshot_child(widget, host->overview, snapshot); gtk_snapshot_pop(snapshot); return;
  }
  gtk_widget_snapshot_child(widget, host->overview, snapshot);
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(gtk_widget_get_width(widget)), static_cast<float>(gtk_widget_get_height(widget)));
  const double view_ratio = view.size.width / view.size.height, thumb_ratio = thumb.size.width / thumb.size.height;
  if (view_ratio > thumb_ratio) { const float width = view.size.height * thumb_ratio; view.origin.x += (view.size.width-width)/2; view.size.width = width; }
  else if (view_ratio < thumb_ratio) { const float height = view.size.width / thumb_ratio; view.origin.y += (view.size.height-height)/2; view.size.height = height; }
  graphene_rect_t clip, transition;
  graphene_rect_interpolate(&bounds, &view, p, &clip);
  const double sx = adw_lerp(1, thumb.size.width/view.size.width, p), sy = adw_lerp(1, thumb.size.height/view.size.height, p);
  transition.size = GRAPHENE_SIZE_INIT(static_cast<float>(clip.size.width*sx), static_cast<float>(clip.size.height*sy));
  transition.origin = GRAPHENE_POINT_INIT(
    thumb.origin.x * (transition.size.width-bounds.size.width)/(thumb.size.width-bounds.size.width),
    thumb.origin.y * (transition.size.height-bounds.size.height)/(thumb.size.height-bounds.size.height));
  GskRoundedRect rounded; gsk_rounded_rect_init_from_rect(&rounded, &transition, adw_lerp(0, 12, p));
  GdkRGBA shade{0,0,0,static_cast<float>(.12*(1-p))}; gtk_snapshot_append_color(snapshot, &shade, &bounds);
  gtk_snapshot_push_rounded_clip(snapshot, &rounded);
  if (host->group_target) gtk_snapshot_push_opacity(snapshot, 1-adw_easing_ease(ADW_EASE_IN_EXPO, p));
  gtk_snapshot_translate(snapshot, &transition.origin); gtk_snapshot_scale(snapshot, sx, sy);
  graphene_point_t origin = GRAPHENE_POINT_INIT(-clip.origin.x, -clip.origin.y); gtk_snapshot_translate(snapshot, &origin);
  gtk_widget_snapshot_child(widget, host->workspace, snapshot);
  if (host->activation_image) {
    // Zoom the selected tab's cached image before mapping its Chromium page.
    // This leaves browser startup, focus and session I/O off the animation path.
    graphene_rect_t page_bounds;
    if (gtk_widget_compute_bounds(host->page, widget, &page_bounds)) {
      gtk_snapshot_save(snapshot); gtk_snapshot_translate(snapshot, &page_bounds.origin);
      gdk_paintable_snapshot(host->activation_image, snapshot, page_bounds.size.width, page_bounds.size.height); gtk_snapshot_restore(snapshot);
    }
  }
  if (host->group_target) gtk_snapshot_pop(snapshot);
  gtk_snapshot_pop(snapshot);
  shade.alpha = .12*adw_easing_ease(ADW_EASE_OUT_EXPO,p);
  gtk_snapshot_append_outset_shadow(snapshot, &rounded, &shade, 0, 0, 1, 0);
}
void host_snapshot(GtkWidget* widget, GtkSnapshot* snapshot) {
  host_snapshot_content(widget, snapshot);
  auto* host = reinterpret_cast<SelectOverviewHost*>(widget);
  if (!host->drop_image) return;
  graphene_rect_t bounds; graphene_rect_interpolate(&host->drop_from, &host->drop_to, host->drop_progress, &bounds);
  gtk_snapshot_save(snapshot); gtk_snapshot_translate(snapshot, &bounds.origin);
  gdk_paintable_snapshot(host->drop_image, snapshot, bounds.size.width, bounds.size.height); gtk_snapshot_restore(snapshot);
}
void host_dispose(GObject* object) {
  auto* host = reinterpret_cast<SelectOverviewHost*>(object);
  if (host->resize_idle) { g_source_remove(host->resize_idle); host->resize_idle = 0; }
  g_clear_object(&host->animation); g_clear_object(&host->activation_image);
  g_clear_object(&host->drop_animation); g_clear_object(&host->drop_image); g_clear_weak_pointer(&host->drop_target);
  g_clear_weak_pointer(&host->thumbnail); g_clear_weak_pointer(&host->page);
  if (host->workspace) { gtk_widget_unparent(host->workspace); host->workspace = nullptr; }
  if (host->overview) { gtk_widget_unparent(host->overview); host->overview = nullptr; }
  G_OBJECT_CLASS(select_overview_host_parent_class)->dispose(object);
}
void select_overview_host_class_init(SelectOverviewHostClass* klass) {
  auto* widget = GTK_WIDGET_CLASS(klass); widget->measure = host_measure; widget->size_allocate = host_allocate; widget->snapshot = host_snapshot;
  g_signal_new("drop-done", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  g_signal_new("transition-done", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  g_signal_new("layout-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_INT);
  gtk_widget_class_set_css_name(widget, "selectoverview"); G_OBJECT_CLASS(klass)->dispose = host_dispose;
}
void select_overview_host_init(SelectOverviewHost* host) {
  auto* target = adw_callback_animation_target_new(+[](double progress, gpointer data) {
    auto* host = static_cast<SelectOverviewHost*>(data); host->progress = progress; gtk_widget_queue_draw(GTK_WIDGET(host));
  }, host, nullptr);
  host->animation = adw_timed_animation_new(GTK_WIDGET(host), 0, 0, 400, target);
  adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(host->animation), ADW_EASE);
  g_signal_connect(host->animation, "done", G_CALLBACK(+[](AdwAnimation*, SelectOverviewHost* host) {
    host->animating = false;
    gtk_widget_set_child_visible(host->overview, host->open);
    gtk_widget_set_can_target(host->workspace, !host->open);
    g_signal_emit_by_name(host, "transition-done");
    g_clear_object(&host->activation_image);
    gtk_widget_queue_draw(GTK_WIDGET(host));
  }), host);
  auto* drop_target = adw_callback_animation_target_new(+[](double progress, gpointer data) {
    auto* host = static_cast<SelectOverviewHost*>(data); host->drop_progress = progress; gtk_widget_queue_draw(GTK_WIDGET(host));
  }, host, nullptr);
  host->drop_animation = adw_timed_animation_new(GTK_WIDGET(host), 0, 1, 250, drop_target);
  adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(host->drop_animation), ADW_EASE_OUT_CUBIC);
  g_signal_connect(host->drop_animation, "done", G_CALLBACK(+[](AdwAnimation*, SelectOverviewHost* host) {
    if (host->drop_target) { gtk_widget_remove_css_class(host->drop_target, "drag-placeholder"); gtk_widget_set_can_target(host->drop_target, true); }
    g_clear_object(&host->drop_image); g_clear_weak_pointer(&host->drop_target);
    g_signal_emit_by_name(host, "drop-done"); gtk_widget_queue_draw(GTK_WIDGET(host));
  }), host);
}
struct Target { WorkspaceTabs* ui; CefRefPtr<Browser> browser; std::string id; };
struct GroupDragPayload { WorkspaceTabs* origin; std::string id; };
GroupDragPayload* group_drag_copy(const GroupDragPayload* value) { return new GroupDragPayload(*value); }
void group_drag_free(GroupDragPayload* value) { delete value; }
G_DEFINE_BOXED_TYPE(GroupDragPayload,select_group_drag,group_drag_copy,group_drag_free)
struct DragContext {
  WorkspaceTabs* ui; CefRefPtr<Browser> browser;
  GtkWidget* tile; std::string id; int x = 0, y = 0;
  DragContext(WorkspaceTabs* ui, CefRefPtr<Browser> browser, GtkWidget* tile, const std::string& id) : ui(ui), browser(browser), tile(tile), id(id) { g_object_add_weak_pointer(G_OBJECT(tile), reinterpret_cast<gpointer*>(&this->tile)); }
  ~DragContext() { if (tile) g_object_remove_weak_pointer(G_OBJECT(tile), reinterpret_cast<gpointer*>(&tile)); }
};
Target* target(GtkWidget* widget, WorkspaceTabs* ui, CefRefPtr<Browser> browser = nullptr, const std::string& id = "") {
  if (auto* existing = static_cast<Target*>(g_object_get_data(G_OBJECT(widget), "target"))) return existing;
  auto* data = new Target{ui, browser, id};
  g_object_set_data_full(G_OBJECT(widget), "target", data, +[](gpointer p) { delete static_cast<Target*>(p); }); return data;
}
std::string folded(const std::string& value) {
  char* text = g_utf8_casefold(value.c_str(), -1); std::string result(text); g_free(text); return result;
}
std::string color_hex(const GdkRGBA& color) {
  char* hex = g_strdup_printf("#%02x%02x%02x",std::clamp(static_cast<int>(std::round(color.red*255)),0,255),std::clamp(static_cast<int>(std::round(color.green*255)),0,255),std::clamp(static_cast<int>(std::round(color.blue*255)),0,255));
  std::string result(hex); g_free(hex); return result;
}
std::string group_class(const std::string& id) { return "select-group-"+id; }
GtkWidget* label(const std::string& text) {
  GtkWidget* widget = gtk_label_new(text.c_str()); gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars(GTK_LABEL(widget), 22); return widget;
}
GtkWidget* button(const char* icon, const char* tooltip) {
  GtkWidget* widget = gtk_button_new_from_icon_name(icon); gtk_widget_add_css_class(widget, "flat");
  gtk_widget_set_tooltip_text(widget, tooltip);
  gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, tooltip, -1); return widget;
}
GtkWidget* group_preview(CefRefPtr<Browser> browser, int width, int height) {
  GtkWidget* preview;
  auto* image = browser ? browser->thumbnail() : nullptr;
  if (image) {
    GtkSnapshot* snapshot = gtk_snapshot_new();
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0,0,static_cast<float>(width),static_cast<float>(height));
    const int sw = gdk_paintable_get_intrinsic_width(image), sh = gdk_paintable_get_intrinsic_height(image);
    const double scale = std::max(static_cast<double>(width)/std::max(1,sw),static_cast<double>(height)/std::max(1,sh));
    const graphene_point_t point = GRAPHENE_POINT_INIT(static_cast<float>((width-sw*scale)/2), static_cast<float>((height-sh*scale)/2));
    gtk_snapshot_push_clip(snapshot, &bounds); gtk_snapshot_translate(snapshot, &point);
    gdk_paintable_snapshot(image, snapshot, sw*scale, sh*scale); gtk_snapshot_pop(snapshot);
    auto* cropped = gtk_snapshot_free_to_paintable(snapshot, &bounds.size); g_object_unref(image);
    preview = preview_picture(cropped,width,height); g_object_unref(cropped);
  } else if (browser) {
    preview = gtk_image_new_from_icon_name("io.github.doromiert.FigmaGtk"); gtk_image_set_pixel_size(GTK_IMAGE(preview), 24);
  } else preview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(preview, width, height); gtk_widget_set_overflow(preview, GTK_OVERFLOW_HIDDEN);
  gtk_widget_add_css_class(preview, "group-preview"); gtk_widget_set_can_target(preview, false); return preview;
}
void clear(GtkWidget* box) { while (GtkWidget* child = gtk_widget_get_first_child(box)) gtk_widget_unparent(child); }
void item(GMenu* menu, const char* title, const char* action, const std::string& value) {
  GMenuItem* entry = g_menu_item_new(title, nullptr);
  g_menu_item_set_action_and_target_value(entry, action, g_variant_new_string(value.c_str()));
  g_menu_append_item(menu, entry); g_object_unref(entry);
}
}

WorkspaceTabs::WorkspaceTabs(Window& window) : win_(window) {
  drag_display_ = GDK_DISPLAY(g_object_ref(gtk_widget_get_display(GTK_WIDGET(win_.window))));
  drag_style_ = gtk_css_provider_new(); gtk_style_context_add_provider_for_display(drag_display_, GTK_STYLE_PROVIDER(drag_style_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);
  group_style_ = gtk_css_provider_new(); gtk_style_context_add_provider_for_display(drag_display_,GTK_STYLE_PROVIDER(group_style_),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+2);
  header_name_ = "select-header-"+std::to_string(reinterpret_cast<uintptr_t>(&win_));
  gtk_widget_set_name(GTK_WIDGET(g_object_get_data(G_OBJECT(win_.window),"main-header")),header_name_.c_str());
  strip_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_widget_add_css_class(strip_, "workspace-tabs");
  strip_scroll_ = gtk_scrolled_window_new(); gtk_widget_set_hexpand(strip_scroll_,true);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(strip_scroll_), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(strip_scroll_), 34);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(strip_scroll_), 34);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(strip_scroll_), true);
  strip_contents_ = GTK_WIDGET(g_object_new(select_tab_strip_get_type(), nullptr));
  gtk_widget_set_hexpand(strip_contents_, true);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(strip_scroll_), strip_contents_);
  gtk_widget_add_css_class(strip_contents_, "workspace-strip"); attach_reorder_drop(strip_contents_);
  scroll_back_ = button("pan-start-symbolic","Scroll Tabs Left"); scroll_forward_ = button("pan-end-symbolic","Scroll Tabs Right");
  gtk_box_append(GTK_BOX(strip_),scroll_back_); gtk_box_append(GTK_BOX(strip_),strip_scroll_); gtk_box_append(GTK_BOX(strip_),scroll_forward_);
  g_object_set_data(G_OBJECT(strip_),"scroller",strip_scroll_); g_object_set_data(G_OBJECT(strip_),"scroll-back",scroll_back_); g_object_set_data(G_OBJECT(strip_),"scroll-forward",scroll_forward_);
  for (auto* control : {scroll_back_,scroll_forward_}) {
    gtk_widget_set_visible(control,false); gtk_widget_set_size_request(control,34,34); gtk_widget_set_valign(control,GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(control),"direction",GINT_TO_POINTER(control == scroll_back_ ? -1 : 1));
    g_signal_connect(control,"clicked",G_CALLBACK(+[](GtkButton* button,WorkspaceTabs* self) { self->scroll_strip(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"direction"))); }),this);
    auto* edge = gtk_drop_target_new(ADW_TYPE_TAB_PAGE,GDK_ACTION_MOVE); GType types[] = {ADW_TYPE_TAB_PAGE,select_group_drag_get_type()}; gtk_drop_target_set_gtypes(edge,types,2);
    g_signal_connect(edge,"enter",G_CALLBACK(+[](GtkDropTarget* drop,double,double,WorkspaceTabs* self) -> GdkDragAction {
      auto* control = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop));
      if (!g_object_get_data(G_OBJECT(control),"drag-scroll")) {
        const guint tick = gtk_widget_add_tick_callback(control,+[](GtkWidget* control,GdkFrameClock*,gpointer p) -> gboolean {
          auto* self = static_cast<WorkspaceTabs*>(p);
          if (!self->drag_tile_ || self->win_.closing || self->win_.destroyed) { g_object_set_data(G_OBJECT(control),"drag-scroll",nullptr); return G_SOURCE_REMOVE; }
          auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->strip_scroll_)); const double maximum = std::max(0.,gtk_adjustment_get_upper(adjustment)-gtk_adjustment_get_page_size(adjustment));
          adw_animation_pause(self->scroll_animation_);
          gtk_adjustment_set_value(adjustment,std::clamp(gtk_adjustment_get_value(adjustment)+12*GPOINTER_TO_INT(g_object_get_data(G_OBJECT(control),"direction")),0.,maximum)); return G_SOURCE_CONTINUE;
        },self,nullptr);
        g_object_set_data(G_OBJECT(control),"drag-scroll",GUINT_TO_POINTER(tick));
      }
      return GDK_ACTION_MOVE;
    }),this);
    g_signal_connect(edge,"leave",G_CALLBACK(+[](GtkDropTarget* drop,gpointer) {
      auto* control = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop)); const guint tick = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(control),"drag-scroll"));
      if (tick) gtk_widget_remove_tick_callback(control,tick);
      g_object_set_data(G_OBJECT(control),"drag-scroll",nullptr);
    }),nullptr); gtk_widget_add_controller(control,GTK_EVENT_CONTROLLER(edge));
    g_signal_connect(edge,"drop",G_CALLBACK(+[](GtkDropTarget* drop,const GValue*,double,double,gpointer) -> gboolean {
      auto* control = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop)); const guint tick = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(control),"drag-scroll"));
      if (tick) gtk_widget_remove_tick_callback(control,tick);
      g_object_set_data(G_OBJECT(control),"drag-scroll",nullptr); return false;
    }),nullptr);
  }
  auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(strip_scroll_));
  auto refresh_scroll = +[](GtkAdjustment*,WorkspaceTabs* self) {
    if (self->scroll_refresh_) return;
    // Adjustment changes can happen during GtkBox allocation. Change button
    // visibility afterwards, so newly shown buttons are measured first.
    self->scroll_refresh_ = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer p) -> gboolean {
      auto* self = static_cast<WorkspaceTabs*>(p); self->scroll_refresh_ = 0;
      if (self->win_.closing || self->win_.destroyed) return G_SOURCE_REMOVE;
      auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->strip_scroll_));
      const double value = gtk_adjustment_get_value(adjustment), maximum = gtk_adjustment_get_upper(adjustment)-gtk_adjustment_get_page_size(adjustment);
      gtk_widget_set_visible(self->scroll_back_,maximum > 1); gtk_widget_set_visible(self->scroll_forward_,maximum > 1);
      gtk_widget_set_sensitive(self->scroll_back_,value > 1); gtk_widget_set_sensitive(self->scroll_forward_,value < maximum-1);
      return G_SOURCE_REMOVE;
    },self,nullptr);
  };
  g_signal_connect(adjustment,"changed",G_CALLBACK(refresh_scroll),this); g_signal_connect(adjustment,"value-changed",G_CALLBACK(refresh_scroll),this);
  auto* scroll_target = adw_callback_animation_target_new(+[](double value,gpointer p) { gtk_adjustment_set_value(GTK_ADJUSTMENT(p),value); },adjustment,nullptr);
  scroll_animation_ = adw_timed_animation_new(strip_,0,0,200,scroll_target); adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(scroll_animation_),ADW_EASE_OUT_CUBIC);
  auto* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  gtk_event_controller_set_propagation_phase(wheel,GTK_PHASE_CAPTURE);
  g_signal_connect(wheel,"scroll",G_CALLBACK(+[](GtkEventControllerScroll* controller,double dx,double dy,WorkspaceTabs* self) -> gboolean {
    auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->strip_scroll_));
    const double maximum = gtk_adjustment_get_upper(adjustment)-gtk_adjustment_get_page_size(adjustment); if (maximum <= 1) return false;
    adw_animation_pause(self->scroll_animation_);
    const double unit = gtk_event_controller_scroll_get_unit(controller) == GDK_SCROLL_UNIT_WHEEL ? 60 : 1;
    gtk_adjustment_set_value(adjustment,std::clamp(gtk_adjustment_get_value(adjustment)+(std::abs(dx) > std::abs(dy) ? dx : dy)*unit,0.,maximum)); return true;
  }),this); gtk_widget_add_controller(strip_,wheel);
  overview_ = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  GtkWidget* search_button = gtk_toggle_button_new(); gtk_button_set_icon_name(GTK_BUTTON(search_button), "edit-find-symbolic");
  gtk_widget_set_tooltip_text(search_button, "Search Tabs"); adw_header_bar_pack_start(ADW_HEADER_BAR(header), search_button);
  overview_title_ = ADW_WINDOW_TITLE(adw_window_title_new("Tabs", ""));
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(overview_title_));
  GtkWidget* overview_menu = gtk_menu_button_new(); gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(overview_menu), "view-more-symbolic");
  GMenu* group_actions = g_menu_new(); g_menu_append(group_actions, "New Tab Group…", "win.new-tab-group");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(overview_menu), G_MENU_MODEL(group_actions)); g_object_unref(group_actions);
  gtk_widget_set_tooltip_text(overview_menu, "Tab Options"); adw_header_bar_pack_end(ADW_HEADER_BAR(header), overview_menu);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(overview_), header);
  search_bar_ = gtk_search_bar_new(); gtk_search_bar_set_key_capture_widget(GTK_SEARCH_BAR(search_bar_), overview_);
  GtkWidget* search = gtk_search_entry_new(); g_object_set(search, "placeholder-text", "Search tabs and groups", nullptr);
  GtkWidget* clamp = adw_clamp_new(); adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 400); gtk_widget_set_hexpand(clamp, true);
  adw_clamp_set_child(ADW_CLAMP(clamp), search); gtk_search_bar_set_child(GTK_SEARCH_BAR(search_bar_), clamp);
  gtk_search_bar_connect_entry(GTK_SEARCH_BAR(search_bar_), GTK_EDITABLE(search));
  g_object_bind_property(search_button, "active", search_bar_, "search-mode-enabled", G_BINDING_BIDIRECTIONAL);
  g_signal_connect(search, "search-changed", G_CALLBACK(+[](GtkSearchEntry* entry, WorkspaceTabs* self) {
    self->query_ = folded(gtk_editable_get_text(GTK_EDITABLE(entry))); self->expanded_.clear(); self->rebuild_overview();
  }), this);
  g_signal_connect(search, "stop-search", G_CALLBACK(+[](GtkSearchEntry*, WorkspaceTabs* self) {
    if (gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(self->search_bar_))) gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(self->search_bar_), false);
    else self->show_overview(false);
  }), this);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(overview_), search_bar_);
  gtk_widget_add_css_class(overview_, "select-tab-overview");
  GtkWidget* scroll = gtk_scrolled_window_new();
  // The grid reflows on the next frame after a window resize. Its previous
  // thumbnail requests must not impose a stale minimum on the whole window.
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
  overview_contents_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_widget_add_css_class(overview_contents_, "overview-grid");
  gtk_widget_set_valign(overview_contents_, GTK_ALIGN_START); gtk_widget_set_vexpand(overview_contents_, false);
  for (auto setter : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) setter(overview_contents_, 5);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), overview_contents_);
  GtkWidget* overlay = gtk_overlay_new(); gtk_overlay_set_child(GTK_OVERLAY(overlay), scroll);
  attach_reorder_drop(overlay);
  GtkWidget* add = gtk_button_new(); GtkWidget* add_content = adw_button_content_new();
  new_tab_button_ = add;
  adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(add_content), "adw-tab-new-symbolic");
  adw_button_content_set_label(ADW_BUTTON_CONTENT(add_content), "New Tab"); gtk_button_set_child(GTK_BUTTON(add), add_content);
  gtk_widget_add_css_class(add, "pill"); gtk_widget_add_css_class(add, "suggested-action"); gtk_widget_add_css_class(add, "new-tab-button");
  gtk_widget_set_halign(add, GTK_ALIGN_CENTER); gtk_widget_set_valign(add, GTK_ALIGN_END);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(add), "win.new-tab"); gtk_overlay_add_overlay(GTK_OVERLAY(overlay), add);
  gtk_widget_set_margin_bottom(overview_contents_, 80);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(overview_), overlay);
  auto* keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType, WorkspaceTabs* self) -> gboolean {
    if (key != GDK_KEY_Escape) return false;
    if (!self->expanded_.empty()) self->expand_overview(""); else self->show_overview(false); return true;
  }), this); gtk_widget_add_controller(overview_, keys);
  attach_reorder_drop(overview_contents_);
  auto* outside = gtk_gesture_click_new();
  g_signal_connect(outside, "pressed", G_CALLBACK(+[](GtkGestureClick*, int, double x, double y, WorkspaceTabs* self) {
    GtkWidget* picked = gtk_widget_pick(self->overview_contents_, x, y, GTK_PICK_DEFAULT);
    for (auto* node = picked; node && node != self->overview_contents_; node = gtk_widget_get_parent(node)) if (GTK_IS_BUTTON(node)) return;
    if (!self->expanded_.empty()) self->expand_overview("");
  }), this);
  gtk_widget_add_controller(overview_contents_, GTK_EVENT_CONTROLLER(outside));
}
WorkspaceTabs::~WorkspaceTabs() {
  if (scroll_refresh_) g_source_remove(scroll_refresh_);
  g_clear_object(&scroll_animation_);
  g_clear_object(&drag_image_);
  gtk_style_context_remove_provider_for_display(drag_display_, GTK_STYLE_PROVIDER(group_style_)); g_object_unref(group_style_);
  gtk_style_context_remove_provider_for_display(drag_display_, GTK_STYLE_PROVIDER(drag_style_)); g_object_unref(drag_style_); g_object_unref(drag_display_);
}
void WorkspaceTabs::scroll_strip(int direction) {
  auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(strip_scroll_));
  const double value = gtk_adjustment_get_value(adjustment), page = gtk_adjustment_get_page_size(adjustment);
  const double destination = std::clamp(value+direction*page*.75,0.,std::max(0.,gtk_adjustment_get_upper(adjustment)-page));
  adw_animation_pause(scroll_animation_); adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(scroll_animation_),value);
  adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(scroll_animation_),destination); adw_animation_play(scroll_animation_);
}
void WorkspaceTabs::begin_drag(GtkWidget* tile) {
  end_drag(); g_set_weak_pointer(&drag_tile_, tile);
  // Keep the actual source slot and move the neighbouring cards to show where
  // the full-size floating card will land. These coordinates survive CSS moves.
  drag_in_overview_ = gtk_widget_has_css_class(tile, "overview-tab") || gtk_widget_has_css_class(tile, "group-card");
  const bool outer = std::find(overview_tiles_.begin(), overview_tiles_.end(), tile) != overview_tiles_.end();
  auto* data = static_cast<Target*>(g_object_get_data(G_OBJECT(tile), "target"));
  if (data && !data->browser) drag_group_ = data->id;
  std::vector<GtkWidget*> candidates;
  if (drag_in_overview_ && outer) candidates = overview_tiles_;
  else if (drag_in_overview_) {
    for (const auto& [browser, main] : preview_buttons_) {
      auto* candidate = gtk_widget_get_ancestor(main, GTK_TYPE_BOX);
      if (candidate && gtk_widget_has_css_class(candidate, "overview-tab") && std::find(overview_tiles_.begin(), overview_tiles_.end(), candidate) == overview_tiles_.end()) candidates.push_back(candidate);
    }
  } else if (gtk_widget_get_parent(tile) == strip_contents_) {
    for (auto* candidate = gtk_widget_get_first_child(strip_contents_); candidate; candidate = gtk_widget_get_next_sibling(candidate)) candidates.push_back(candidate);
  } else for (const auto& [browser, candidate] : strip_tabs_) if (gtk_widget_get_parent(candidate) == gtk_widget_get_parent(tile)) candidates.push_back(candidate);
  for (auto* candidate : candidates) {
    graphene_rect_t bounds;
    if (gtk_widget_compute_bounds(candidate, drag_in_overview_ ? overview_contents_ : strip_contents_, &bounds)) drag_positions_.push_back({candidate, bounds.origin});
  }
  std::sort(drag_positions_.begin(), drag_positions_.end(), [](const auto& a, const auto& b) { return a.point.y == b.point.y ? a.point.x < b.point.x : a.point.y < b.point.y; });
  for (auto& position : drag_positions_) {
    const auto name = "select-preview-"+std::to_string(reinterpret_cast<uintptr_t>(position.tile)); gtk_widget_set_name(position.tile, name.c_str());
  }
  gtk_widget_add_css_class(tile, "drag-placeholder");
}
void WorkspaceTabs::hover_drag(GtkWidget* tile) {
  if (!drag_tile_ || tile == drag_hover_) return;
  drag_hover_ = tile;
  auto source = std::find_if(drag_positions_.begin(), drag_positions_.end(), [&](const auto& item) { return item.tile == drag_tile_; });
  auto destination = std::find_if(drag_positions_.begin(), drag_positions_.end(), [&](const auto& item) { return item.tile == tile; });
  if (source == drag_positions_.end()) return;
  const int from = source-drag_positions_.begin(), to = destination == drag_positions_.end() ? from : destination-drag_positions_.begin(); std::ostringstream css;
  gboolean animations = true; g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &animations, nullptr);
  for (int i = 0; i < static_cast<int>(drag_positions_.size()); ++i) {
    if (i == from) continue;
    int target = i;
    if (from < to && i > from && i <= to) target = i-1;
    if (from > to && i >= to && i < from) target = i+1;
    const auto& item = drag_positions_[i]; const auto& point = drag_positions_[target].point;
    css << "#" << gtk_widget_get_name(item.tile) << " { transform: translate(" << point.x-item.point.x << "px," << point.y-item.point.y << "px); transition: transform " << (animations ? 250 : 0) << "ms cubic-bezier(.25,.1,.25,1); }";
  }
  gtk_css_provider_load_from_string(drag_style_, css.str().c_str());
}
void WorkspaceTabs::end_drag() {
  for (auto* control : {scroll_back_,scroll_forward_}) {
    const guint tick = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(control),"drag-scroll"));
    if (tick) gtk_widget_remove_tick_callback(control,tick);
    g_object_set_data(G_OBJECT(control),"drag-scroll",nullptr);
  }
  if (drag_merge_) gtk_widget_remove_css_class(drag_merge_, "group-drop-candidate");
  drag_merge_ = nullptr;
  if (drag_tile_) gtk_widget_remove_css_class(drag_tile_, "drag-placeholder");
  g_clear_weak_pointer(&drag_tile_); drag_ungroup_ = false; drag_group_.clear(); drag_hover_ = nullptr; drag_positions_.clear(); gtk_css_provider_load_from_string(drag_style_, "");
}
SavedGroup* WorkspaceTabs::group(const std::string& id) {
  auto& groups = win_.app.state.groups;
  auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.id == id; });
  return found == groups.end() ? nullptr : &*found;
}
GdkRGBA WorkspaceTabs::group_color(const std::string& id) {
  GdkRGBA color{.57f,.25f,.67f,1};
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_style_context_lookup_color(gtk_widget_get_style_context(GTK_WIDGET(win_.window)),"accent_bg_color",&color);
  G_GNUC_END_IGNORE_DEPRECATIONS
  if (auto* g = group(id); g && !g->color.empty()) gdk_rgba_parse(&color,g->color.c_str());
  return color;
}
void WorkspaceTabs::update_colors(const std::string& id) {
  header_group_ = id; std::ostringstream css;
  for (const auto& g : win_.app.state.groups) css << "." << group_class(g.id) << " { --select-group-color: " << color_hex(group_color(g.id)) << "; }";
  auto* header = GTK_WIDGET(g_object_get_data(G_OBJECT(win_.window),"main-header"));
  if (id.empty()) gtk_widget_remove_css_class(header,"select-group-header");
  else {
    gtk_widget_add_css_class(header,"select-group-header");
    css << "#" << header_name_ << ".select-group-header { background-color: color-mix(in srgb, " << color_hex(group_color(id)) << " 22%, var(--headerbar-bg-color)); }";
  }
  gtk_css_provider_load_from_string(group_style_,css.str().c_str());
}
void WorkspaceTabs::color_dialog(const std::string& id) {
  if (!group(id)) return;
  GtkColorDialog* dialog = gtk_color_dialog_new(); gtk_color_dialog_set_title(dialog,"Tab Group Color"); gtk_color_dialog_set_with_alpha(dialog,false);
  struct ColorChoice { WorkspaceTabs* ui; std::string id; }; auto* choice = new ColorChoice{this,id}; const auto color = group_color(id);
  gtk_color_dialog_choose_rgba(dialog,win_.window,&color,nullptr,+[](GObject* source,GAsyncResult* result,gpointer p) {
    std::unique_ptr<ColorChoice> choice(static_cast<ColorChoice*>(p)); GError* error = nullptr;
    auto* color = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(source),result,&error);
    if (color && !choice->ui->win_.destroyed) if (auto* g = choice->ui->group(choice->id)) { g->color = color_hex(*color); choice->ui->changed(); }
    if (color) gdk_rgba_free(color);
    g_clear_error(&error);
  },choice); g_object_unref(dialog);
}
std::vector<CefRefPtr<Browser>> WorkspaceTabs::pages() const {
  std::vector<CefRefPtr<Browser>> result;
  for (int i = 0; i < adw_tab_view_get_n_pages(win_.tabs); ++i) {
    auto* page = adw_tab_view_get_nth_page(win_.tabs, i);
    if (auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"))) result.push_back(browser);
  }
  return result;
}
void WorkspaceTabs::update() {
  if (settling_ || normalizing_ || drag_tile_) return;
  normalize_order();
  const auto all = pages();
  if (!expanded_.empty() && std::none_of(all.begin(),all.end(),[&](const auto& b) { return b->group_id == expanded_; })) expanded_.clear();
  for (auto it = last_group_tabs_.begin(); it != last_group_tabs_.end();) {
    const bool valid = std::any_of(all.begin(), all.end(), [&](const auto& b) { return b.get() == it->second && !b->closing && b->group_id == it->first; });
    if (!valid) it = last_group_tabs_.erase(it); else ++it;
  }
  auto current = win_.current();
  const std::string current_group = current && !current->is_home ? current->group_id : "";
  if (current && !current->closing && !current->is_home && group(current_group)) last_group_tabs_[current_group] = current.get();
  // A manual collapse remains collapsed until the selected tab changes.
  if (current.get() != last_current_ || current_group != last_current_group_) {
    if (pages_host_ && current.get() != last_current_) {
      const auto old_tab = std::find_if(all.begin(), all.end(), [&](const auto& b) { return b.get() == last_current_; });
      const auto new_tab = std::find(all.begin(), all.end(), current);
      const bool zooming = overview_open_ || pending_activation_ || (root_ && reinterpret_cast<SelectOverviewHost*>(root_)->animating);
      const bool old_valid = last_current_ && (old_tab != all.end() || last_current_ == win_.home_browser_.get());
      const bool new_valid = current && (new_tab != all.end() || current == win_.home_browser_);
      // Home occupies the position immediately before the document tabs.
      const int old_position = old_tab == all.end() ? -1 : old_tab-all.begin();
      const int new_position = new_tab == all.end() ? -1 : new_tab-all.begin();
      const int direction = !zooming && old_valid && new_valid ? (new_position > old_position ? 1 : -1) : 0;
      page_slide_start(reinterpret_cast<SelectPageSlide*>(pages_host_), direction);
    }
    if (current && !current->is_home) expand_header(current_group);
    last_current_ = current.get(); last_current_group_ = current_group;
  }
  std::ostringstream value; value << win_.current().get();
  for (auto& b : pages()) value << b.get() << b->name << b->loading << b->group_id;
  for (const auto& g : win_.app.state.groups) value << g.id << g.title << g.collapsed << g.color;
  if (value.str() == signature_) return;
  signature_ = value.str(); rebuild_strip();
  if (overview_open_ && !selecting_) rebuild_overview();
}
void WorkspaceTabs::normalize_order() {
  // The visual group order and keyboard tab order use the same model. Moving
  // a distant tab into a group makes its members contiguous in AdwTabView.
  const auto all = pages(); std::vector<CefRefPtr<Browser>> ordered; std::set<std::string> seen;
  for (const auto& browser : all) {
    if (!group(browser->group_id)) ordered.push_back(browser);
    else if (seen.insert(browser->group_id).second)
      for (const auto& member : all) if (member->group_id == browser->group_id) ordered.push_back(member);
  }
  normalizing_ = true;
  for (size_t i = 0; i < ordered.size(); ++i) {
    auto* page = adw_tab_view_get_page(win_.tabs, ordered[i]->widget());
    if (adw_tab_view_get_page_position(win_.tabs, page) != static_cast<int>(i)) adw_tab_view_reorder_page(win_.tabs, page, i);
  }
  normalizing_ = false;
}
void WorkspaceTabs::changed() {
  for (auto& window : win_.app.windows) if (!window->destroyed) {
    window->workspace->signature_.clear(); window->update();
  }
  // A new Window can receive page-attached before it is in Application::windows.
  update(); win_.app.persist(); win_.app.queue_save();
}
void WorkspaceTabs::activate(CefRefPtr<Browser> browser) {
  if (!browser || browser->closing || browser->owner() != &win_) return;
  if (overview_open_) {
    pending_activation_ = browser;
    auto* host = reinterpret_cast<SelectOverviewHost*>(root_);
    auto* image = browser->thumbnail(); g_set_object(&host->activation_image, image); if (image) g_object_unref(image);
    show_overview(false); return;
  }
  if (browser->is_home) { win_.native_home(); if (auto current = win_.current()) gtk_widget_grab_focus(current->surface()); return; }
  expand_header(browser->group_id);
  auto* page = adw_tab_view_get_page(win_.tabs, browser->widget());
  selecting_ = true; adw_tab_view_set_selected_page(win_.tabs, page); selecting_ = false;
  gtk_stack_set_visible_child_name(GTK_STACK(win_.content_), "documents");
  show_overview(false); win_.update(); gtk_widget_grab_focus(browser->surface());
}
void WorkspaceTabs::new_tab() {
  const auto current = win_.current();
  const auto group_id = current && !current->is_home ? current->group_id : std::string();
  if (!overview_open_) {
    auto browser = win_.open(Policy::new_tab_url);
    if (!group_id.empty()) assign(browser, group_id);
    activate(browser); return;
  }
  // Keep the outgoing overview and its button alive until the zoom finishes.
  // Selecting the new browser earlier chooses the previous card as the zoom
  // origin and maps Chromium while the animation is starting.
  selecting_ = true;
  auto browser = win_.open(Policy::new_tab_url, false, true);
  if (!group_id.empty()) assign(browser, group_id);
  selecting_ = false; pending_activation_ = browser; zoom_from_new_tab_ = true;
  auto* host = reinterpret_cast<SelectOverviewHost*>(root_);
  GtkSnapshot* snapshot = gtk_snapshot_new(); GdkRGBA background{.13f,.13f,.14f,1};
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_style_context_lookup_color(gtk_widget_get_style_context(root_), "window_bg_color", &background);
  G_GNUC_END_IGNORE_DEPRECATIONS
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(gtk_widget_get_width(win_.content_)), static_cast<float>(gtk_widget_get_height(win_.content_)));
  gtk_snapshot_append_color(snapshot, &background, &bounds);
  auto* image = gtk_snapshot_free_to_paintable(snapshot, &bounds.size); g_set_object(&host->activation_image, image); g_object_unref(image);
  show_overview(false);
}
std::string WorkspaceTabs::create_group(const std::string& title, CefRefPtr<Browser> browser, const std::string& color) {
  if (!browser || browser->is_home || browser->closing || browser->owner() != &win_ || title.empty()) return {};
  char* uuid = g_uuid_string_random(); std::string id(uuid); g_free(uuid);
  win_.app.state.groups.push_back({id, title, overview_open_,color}); browser->group_id = id;
  last_group_tabs_[id] = browser.get();
  if (!overview_open_) activate(browser);
  changed(); return id;
}
void WorkspaceTabs::assign(CefRefPtr<Browser> browser, const std::string& id) {
  if (!browser || browser->is_home || browser->closing || browser->owner() != &win_ || (!id.empty() && !group(id))) return;
  browser->group_id = id; changed();
}
void WorkspaceTabs::toggle_group(const std::string& id) {
  if (auto* g = group(id)) {
    if (g->collapsed) { select_group(id); return; }
    g->collapsed = true; changed();
    if (auto* widget = group_button(id)) gtk_widget_grab_focus(widget);
  }
}
void WorkspaceTabs::expand_header(const std::string& id) {
  std::set<std::string> present;
  for (const auto& b : pages()) present.insert(b->group_id);
  for (auto& g : win_.app.state.groups) if (present.contains(g.id)) g.collapsed = g.id != id;
}
void WorkspaceTabs::select_group(const std::string& id) {
  const auto all = pages();
  const auto remembered = last_group_tabs_.find(id);
  CefRefPtr<Browser> selected;
  for (const auto& b : all) if (b->group_id == id && !b->closing) {
    selected = b;
    if (remembered != last_group_tabs_.end() && remembered->second == b.get()) break;
  }
  if (!selected) return;
  activate(selected); changed();
}
void WorkspaceTabs::ungroup(const std::string& id) {
  for (auto& window : win_.app.windows) for (auto& b : window->browsers_) if (b->group_id == id) b->group_id.clear();
  auto& groups = win_.app.state.groups;
  groups.erase(std::remove_if(groups.begin(), groups.end(), [&](const auto& g) { return g.id == id; }), groups.end()); changed();
}
void WorkspaceTabs::name_dialog(CefRefPtr<Browser> browser, const std::string& id) {
  auto* g = group(id);
  if (browser && browser->is_home) browser = nullptr;
  if (!id.empty() && !g) return;
  AdwDialog* dialog = adw_alert_dialog_new(g ? "Rename Tab Group" : "New Tab Group", nullptr);
  if (!g && !browser) adw_alert_dialog_set_body(ADW_ALERT_DIALOG(dialog), "Creates a group with a new file tab.");
  GtkWidget* entry = gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(entry), 96);
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Group name");
  gtk_editable_set_text(GTK_EDITABLE(entry), g ? g->title.c_str() : "New group");
  GtkWidget* extra = gtk_box_new(GTK_ORIENTATION_VERTICAL,12); gtk_box_append(GTK_BOX(extra),entry);
  GtkWidget* color_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12); GtkWidget* color_label = gtk_label_new("Color");
  gtk_widget_set_hexpand(color_label,true); gtk_label_set_xalign(GTK_LABEL(color_label),0); gtk_box_append(GTK_BOX(color_row),color_label);
  GtkColorDialog* picker = gtk_color_dialog_new(); gtk_color_dialog_set_title(picker,"Tab Group Color"); gtk_color_dialog_set_with_alpha(picker,false);
  GtkWidget* color_button = gtk_color_dialog_button_new(picker); const auto color = group_color(id);
  gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(color_button),&color); gtk_box_append(GTK_BOX(color_row),color_button); gtk_box_append(GTK_BOX(extra),color_row);
  g_object_set_data(G_OBJECT(dialog),"group-color",color_button); adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog),extra);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "save", g ? "_Rename" : "_Create", nullptr);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save"); adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  auto* data = target(GTK_WIDGET(dialog), this, browser, id);
  g_object_set_data(G_OBJECT(dialog), "entry", entry);
  g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable* entry, AdwAlertDialog* dialog) {
    char* text = g_strdup(gtk_editable_get_text(entry)); g_strstrip(text);
    adw_alert_dialog_set_response_enabled(dialog, "save", *text != 0); g_free(text);
  }), dialog);
  g_signal_connect(dialog, "response", G_CALLBACK(+[](AdwAlertDialog* dialog, const char* response, Target* data) {
    if (std::strcmp(response, "save") || data->ui->win_.destroyed) return;
    char* text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "entry")))); g_strstrip(text);
    if (*text) {
      if (data->id.empty()) {
        auto* ui = data->ui; auto browser = data->browser;
        if (!browser) {
          ui->selecting_ = true; browser = ui->win_.open(Policy::new_tab_url, !ui->overview_open_, ui->overview_open_); ui->selecting_ = false;
        }
        const auto id = ui->create_group(text,browser,color_hex(*gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(g_object_get_data(G_OBJECT(dialog),"group-color")))));
        if (ui->overview_open_ && !id.empty()) ui->expand_overview(id);
      }
      else if (auto* g = data->ui->group(data->id)) { g->title = text; g->color = color_hex(*gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(g_object_get_data(G_OBJECT(dialog),"group-color")))); data->ui->changed(); }
    }
    g_free(text);
  }), data);
  adw_dialog_set_focus(dialog, entry); adw_dialog_present(dialog, GTK_WIDGET(win_.window));
}
GtkWidget* WorkspaceTabs::group_button(const std::string& id) const {
  auto found = group_buttons_.find(id); return found == group_buttons_.end() ? nullptr : found->second;
}

GtkWidget* WorkspaceTabs::tab(CefRefPtr<Browser> browser, bool preview) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, preview ? "overview-tab" : "document-tab");
  if (!preview) strip_tabs_[browser.get()] = box;
  if (dropping_group_.empty() && dropping_browser_.get() == browser.get() && preview == dropping_in_overview_) gtk_widget_add_css_class(box, "drag-placeholder");
  const bool selected = win_.current().get() == browser.get();
  if (selected) gtk_widget_add_css_class(box, "selected");
  GtkWidget* main = GTK_WIDGET(g_object_new(select_tab_button_get_type(), nullptr));
  gtk_widget_add_css_class(main, "flat"); gtk_widget_add_css_class(main, "tab-label"); gtk_widget_set_hexpand(main, true);
  gtk_accessible_update_state(GTK_ACCESSIBLE(main), GTK_ACCESSIBLE_STATE_SELECTED, selected, -1);
  gtk_widget_set_tooltip_text(main, browser->name.c_str());
  auto* data = target(main, this, browser);
  GtkWidget* title = label(browser->name);
  if (preview) {
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GdkPaintable* image = browser->thumbnail(); GtkWidget* picture;
    const double ratio = std::clamp(static_cast<double>(std::max(1, gtk_widget_get_width(win_.content_)))/std::max(1, gtk_widget_get_height(win_.content_)), .8, 2.7);
    const int width = tile_width_-12, height = width/ratio;
    if (image) {
      // Keep the full-resolution texture in a scaled render node; only the
      // paintable's logical requisition shrinks to the native thumbnail size.
      GtkSnapshot* snapshot = gtk_snapshot_new(); graphene_rect_t bounds = GRAPHENE_RECT_INIT(0,0,static_cast<float>(width),static_cast<float>(height));
      gtk_snapshot_push_clip(snapshot, &bounds);
      const double source_ratio = gdk_paintable_get_intrinsic_aspect_ratio(image);
      const double iw = source_ratio > ratio ? height*source_ratio : width, ih = source_ratio > ratio ? height : width/source_ratio;
      graphene_point_t origin = GRAPHENE_POINT_INIT(static_cast<float>((width-iw)/2), static_cast<float>((height-ih)/2)); gtk_snapshot_translate(snapshot, &origin);
      gdk_paintable_snapshot(image, snapshot, iw, ih); gtk_snapshot_pop(snapshot);
      graphene_size_t size = GRAPHENE_SIZE_INIT(static_cast<float>(width),static_cast<float>(height)); auto* cropped = gtk_snapshot_free_to_paintable(snapshot, &size);
      picture = preview_picture(cropped,width,height); g_object_unref(cropped); g_object_unref(image);
    }
    else { picture = gtk_image_new_from_icon_name("io.github.doromiert.FigmaGtk"); gtk_image_set_pixel_size(GTK_IMAGE(picture), 48); gtk_widget_add_css_class(picture, "dim-label"); }
    gtk_widget_add_css_class(picture, "thumbnail"); gtk_widget_set_overflow(picture, GTK_OVERFLOW_HIDDEN);
    previews_[browser.get()] = picture; preview_buttons_[browser.get()] = main;
    gtk_widget_set_size_request(picture, width, height); gtk_box_append(GTK_BOX(content), picture); gtk_box_append(GTK_BOX(content), title);
    gtk_widget_set_margin_bottom(title, 6); gtk_button_set_child(GTK_BUTTON(main), content);
  } else {
    gtk_widget_set_margin_start(title, 34); gtk_widget_set_margin_end(title, 34);
    gtk_button_set_child(GTK_BUTTON(main), title);
  }
  g_signal_connect(main, "clicked", G_CALLBACK(+[](GtkButton*, Target* data) {
    auto* ui = data->ui; auto browser = data->browser;
    if (ui->overview_open_ && !ui->expanded_.empty() && browser->group_id != ui->expanded_) ui->expand_overview("");
    else ui->activate(browser);
  }), data);
  GtkWidget* close = button("window-close-symbolic", "Close Tab"); gtk_widget_add_css_class(close, "tab-close");
  auto* close_data = target(close, this, browser);
  g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton*, Target* data) {
    auto browser = data->browser;
    if (!browser->closing && browser->owner() == &data->ui->win_) {
      auto* page = adw_tab_view_get_page(data->ui->win_.tabs, browser->widget()); adw_tab_view_close_page(data->ui->win_.tabs, page);
    }
  }), close_data);
  auto* middle = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), 2);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(middle), GTK_PHASE_CAPTURE);
  g_signal_connect(middle, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double, double, gpointer) {
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  }), nullptr);
  g_signal_connect(middle, "released", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, Target* data) {
    auto* tile = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)); auto browser = data->browser;
    if (x >= 0 && y >= 0 && x < gtk_widget_get_width(tile) && y < gtk_widget_get_height(tile) && !browser->closing && browser->owner() == &data->ui->win_) {
      adw_tab_view_close_page(data->ui->win_.tabs, adw_tab_view_get_page(data->ui->win_.tabs, browser->widget()));
    }
  }), target(box, this, browser)); gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(middle));
  GtkWidget* overlay = gtk_overlay_new(); gtk_overlay_set_child(GTK_OVERLAY(overlay), main);
  gtk_widget_set_halign(close, GTK_ALIGN_END); gtk_widget_set_valign(close, preview ? GTK_ALIGN_START : GTK_ALIGN_CENTER);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), close); gtk_box_append(GTK_BOX(box), overlay);
  if (!preview) {
    auto* icon = gtk_button_get_child(GTK_BUTTON(close)); gtk_widget_set_halign(icon, GTK_ALIGN_CENTER); gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
  }
  auto* hover = gtk_event_controller_motion_new();
  g_signal_connect(hover, "enter", G_CALLBACK(+[](GtkEventControllerMotion*, double, double, GtkWidget* box) { gtk_widget_add_css_class(box, "hovering"); }), box);
  g_signal_connect(hover, "leave", G_CALLBACK(+[](GtkEventControllerMotion*, GtkWidget* box) { gtk_widget_remove_css_class(box, "hovering"); }), box);
  gtk_widget_add_controller(box, hover);
  auto* menu = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), 3);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(menu), GTK_PHASE_CAPTURE);
  g_signal_connect(menu, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, Target* data) {
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    data->ui->tab_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)), data->browser, x, y);
  }), target(box, this, browser)); gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(menu));
  attach_drag(main, box, browser);
  auto* drop = gtk_drop_target_new(ADW_TYPE_TAB_PAGE, GDK_ACTION_MOVE);
  GType types[] = {ADW_TYPE_TAB_PAGE, select_group_drag_get_type()}; gtk_drop_target_set_gtypes(drop, types, 2);
  g_signal_connect(drop, "motion", G_CALLBACK(+[](GtkDropTarget* drop, double, double, Target* data) -> GdkDragAction {
    data->ui->hover_drag(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop))); return GDK_ACTION_MOVE;
  }), target(box, this, browser));
  g_signal_connect(drop, "drop", G_CALLBACK(+[](GtkDropTarget* controller, const GValue* value, double x, double y, Target* data) -> gboolean {
    return data->ui->drop_on(value, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), x, y, data->browser, data->id);
  }), target(box, this, browser)); gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(drop));
  if (preview) { gtk_widget_set_size_request(box, tile_width_, -1); gtk_widget_set_halign(box, GTK_ALIGN_CENTER); }
  gtk_widget_set_hexpand(box, !preview); gtk_widget_set_valign(box, GTK_ALIGN_CENTER); return box;
}
void WorkspaceTabs::attach_drag(GtkWidget* source, GtkWidget* tile, CefRefPtr<Browser> browser, const std::string& id) {
  GtkDragSource* drag = gtk_drag_source_new(); gtk_drag_source_set_actions(drag, GDK_ACTION_MOVE);
  // The window handle must not move the window while the tab's drag source
  // waits for GTK's DND threshold. Motion bubbles through the tab first.
  g_signal_connect(drag, "update", G_CALLBACK(+[](GtkGesture* gesture, GdkEventSequence*, gpointer) {
    for (auto* parent = gtk_widget_get_parent(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture))); parent; parent = gtk_widget_get_parent(parent)) {
      if (!GTK_IS_WINDOW_HANDLE(parent)) continue;
      GListModel* controllers = gtk_widget_observe_controllers(parent);
      for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
        auto* controller = G_OBJECT(g_list_model_get_item(controllers, i));
        if (GTK_IS_GESTURE_DRAG(controller)) gtk_gesture_set_state(GTK_GESTURE(controller), GTK_EVENT_SEQUENCE_DENIED);
        g_object_unref(controller);
      }
      g_object_unref(controllers);
    }
  }), nullptr);
  auto* drag_data = new DragContext(this, browser, tile, id);
  g_object_set_data_full(G_OBJECT(drag), "drag-context", drag_data, +[](gpointer p) { delete static_cast<DragContext*>(p); });
  g_signal_connect(drag, "prepare", G_CALLBACK((+[](GtkDragSource* source, double x, double y, DragContext* data) -> GdkContentProvider* {
    if (!data->tile) return nullptr;
    if (!data->id.empty()) {
      data->browser = nullptr;
      for (const auto& member : data->ui->pages()) if (member->group_id == data->id && !member->closing) { data->browser = member; break; }
    }
    if (!data->browser || data->browser->owner() != &data->ui->win_ || data->browser->closing) return nullptr;
    graphene_point_t press = GRAPHENE_POINT_INIT(static_cast<float>(x), static_cast<float>(y)); graphene_point_t point;
    if (!gtk_widget_compute_point(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source)), data->tile, &press, &point)) return nullptr;
    graphene_rect_t tile_bounds;
    if (!gtk_widget_compute_bounds(data->tile, data->tile, &tile_bounds)) return nullptr;
    data->x = point.x-tile_bounds.origin.x; data->y = point.y-tile_bounds.origin.y;
    if (data->ui->win_.app.render_test && g_getenv("FIGMA_GTK_TEST_HEADER_DRAG")) g_print("Header drag prepare\n");
    auto* capture = gtk_widget_paintable_new(data->tile); auto* image = gdk_paintable_get_current_image(capture);
    if (!gtk_widget_has_css_class(data->tile, "overview-tab")) {
      // Retain the source surface under translucent tabs and group previews.
      const bool overview = gtk_widget_has_css_class(data->tile, "group-card");
      GtkSnapshot* snapshot = gtk_snapshot_new();
      GdkRGBA background = adw_style_manager_get_dark(adw_style_manager_get_default()) ? GdkRGBA{.13f,.13f,.14f,1} : GdkRGBA{.95f,.95f,.96f,1};
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_style_context_lookup_color(gtk_widget_get_style_context(data->tile), overview ? "overview_bg_color" : "headerbar_bg_color", &background);
      if (!overview && !data->ui->header_group_.empty()) {
        const auto accent = data->ui->group_color(data->ui->header_group_); constexpr float tint = .22f;
        background.red = background.red*(1-tint)+accent.red*tint;
        background.green = background.green*(1-tint)+accent.green*tint;
        background.blue = background.blue*(1-tint)+accent.blue*tint;
      }
      G_GNUC_END_IGNORE_DEPRECATIONS
      graphene_rect_t bounds = GRAPHENE_RECT_INIT(0,0,tile_bounds.size.width,tile_bounds.size.height);
      GskRoundedRect rounded; gsk_rounded_rect_init_from_rect(&rounded, &bounds, overview ? 16 : data->id.empty() ? 6 : 9);
      gtk_snapshot_push_rounded_clip(snapshot, &rounded); gtk_snapshot_append_color(snapshot, &background, &bounds);
      gdk_paintable_snapshot(image, snapshot, bounds.size.width, bounds.size.height); gtk_snapshot_pop(snapshot);
      g_object_unref(image); image = gtk_snapshot_free_to_paintable(snapshot, &bounds.size);
    }
    g_set_object(&data->ui->drag_image_, image); data->ui->drag_hotspot_x_ = data->x; data->ui->drag_hotspot_y_ = data->y;
    gtk_drag_source_set_icon(source, image, data->x, data->y); g_object_unref(image); g_object_unref(capture);
    if (!data->id.empty()) { GroupDragPayload payload{data->ui,data->id}; return gdk_content_provider_new_typed(select_group_drag_get_type(),&payload); }
    return gdk_content_provider_new_typed(ADW_TYPE_TAB_PAGE, adw_tab_view_get_page(data->ui->win_.tabs, data->browser->widget()));
  })), drag_data);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(+[](GtkDragSource*, GdkDrag*, DragContext* data) {
    if (data->tile) { if (data->ui->win_.app.render_test && g_getenv("FIGMA_GTK_TEST_HEADER_DRAG")) g_print("Header drag begin\n"); data->ui->begin_drag(data->tile); }
  }), drag_data);
  g_signal_connect(drag,"drag-cancel",G_CALLBACK((+[](GtkDragSource*,GdkDrag* drag,GdkDragCancelReason reason,DragContext* data) -> gboolean {
    if (reason != GDK_DRAG_CANCEL_NO_TARGET || data->ui->win_.closing || data->ui->win_.destroyed) return false;
    auto* device = gdk_drag_get_device(drag);
    if (device && gdk_device_get_surface_at_position(device,nullptr,nullptr) == gtk_native_get_surface(GTK_NATIVE(data->ui->win_.window))) return false;
    struct DetachRequest { WorkspaceTabs* ui; CefRefPtr<Browser> browser; std::string id; };
    auto* request = new DetachRequest{data->ui,data->browser,data->id};
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer p) -> gboolean {
      auto* request = static_cast<DetachRequest*>(p); if (!request->ui->win_.closing && !request->ui->win_.destroyed) request->ui->detach(request->browser,request->id); return G_SOURCE_REMOVE;
    },request,+[](gpointer p) { delete static_cast<DetachRequest*>(p); }); return true;
  })),drag_data);
  g_signal_connect(drag, "drag-end", G_CALLBACK(+[](GtkDragSource*, GdkDrag*, gboolean, DragContext* data) {
    auto* ui = data->ui; ui->end_drag();
    // Apply title/load changes deferred during the drag, after GTK releases its
    // source controller. Accepted drops keep their widgets until settling ends.
    gtk_widget_add_tick_callback(ui->root_, +[](GtkWidget*, GdkFrameClock*, gpointer p) -> gboolean {
      auto* ui = static_cast<WorkspaceTabs*>(p); if (!ui->win_.destroyed && !ui->win_.closing) ui->update(); return G_SOURCE_REMOVE;
    }, ui, nullptr);
  }), drag_data);
  gtk_widget_add_controller(source, GTK_EVENT_CONTROLLER(drag));
}
void WorkspaceTabs::group_drop(GtkWidget* widget, const std::string& id) {
  auto* drop = gtk_drop_target_new(ADW_TYPE_TAB_PAGE, GDK_ACTION_MOVE);
  GType types[] = {ADW_TYPE_TAB_PAGE, select_group_drag_get_type()}; gtk_drop_target_set_gtypes(drop, types, 2);
  g_signal_connect(drop, "motion", G_CALLBACK(+[](GtkDropTarget* drop, double, double, Target* data) -> GdkDragAction {
    data->ui->hover_drag(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop))); return GDK_ACTION_MOVE;
  }), target(widget, this, nullptr, id));
  g_signal_connect(drop, "drop", G_CALLBACK(+[](GtkDropTarget* controller, const GValue* value, double x, double y, Target* data) -> gboolean {
    return data->ui->drop_on(value, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), x, y, nullptr, data->id);
  }), target(widget, this, nullptr, id)); gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drop));
}
bool WorkspaceTabs::drop_on(const GValue* value, GtkWidget* widget, double x, double y, CefRefPtr<Browser> destination, const std::string& target_id) {
  const auto all = pages();
  if (G_VALUE_HOLDS(value,select_group_drag_get_type())) {
    auto* payload = static_cast<GroupDragPayload*>(g_value_get_boxed(value)); if (!payload || !payload->origin) return false;
    auto* origin = payload->origin; const auto id = payload->id;
    if (id.empty() || id != origin->drag_group_ || !group(id) || &origin->win_.app != &win_.app || origin->win_.closing) return false;
    if (origin != this) {
      CefRefPtr<Browser> member; for (const auto& b : origin->pages()) if (b->group_id == id && !b->closing) { member = b; break; }
      if (!member) return false;
      int position = destination ? adw_tab_view_get_page_position(win_.tabs,adw_tab_view_get_page(win_.tabs,destination->widget())) : adw_tab_view_get_n_pages(win_.tabs);
      if (!destination && !target_id.empty()) for (const auto& b : all) if (b->group_id == target_id) position = adw_tab_view_get_page_position(win_.tabs,adw_tab_view_get_page(win_.tabs,b->widget()))+1;
      prepare_drop(member,widget,x,y,id); origin->end_drag(); receive(member,id,origin->win_,position); return true;
    }
    std::vector<CefRefPtr<Browser>> members, ordered;
    for (const auto& b : all) (b->group_id == id ? members : ordered).push_back(b);
    if (members.empty()) return false;
    const std::string destination_group = destination ? destination->group_id : target_id;
    if (destination_group == id) { prepare_drop(members.front(), widget, x, y, id); end_drag(); changed(); return true; }
    if (!destination) for (const auto& b : ordered) if (b->group_id == destination_group) { destination = b; break; }
    if (!destination || destination->owner() != &win_) return false;
    const auto source_at = std::find(all.begin(), all.end(), members.front());
    const auto destination_at = std::find(all.begin(), all.end(), destination);
    auto position = std::find(ordered.begin(), ordered.end(), destination);
    if (!destination_group.empty()) {
      position = std::find_if(ordered.begin(), ordered.end(), [&](const auto& b) { return b->group_id == destination_group; });
      if (source_at < destination_at) while (position != ordered.end() && (*position)->group_id == destination_group) ++position;
    } else if (source_at < destination_at && position != ordered.end()) ++position;
    ordered.insert(position, members.begin(), members.end());
    prepare_drop(members.front(), widget, x, y, id); end_drag();
    normalizing_ = true;
    for (size_t i = 0; i < ordered.size(); ++i) adw_tab_view_reorder_page(win_.tabs, adw_tab_view_get_page(win_.tabs, ordered[i]->widget()), i);
    normalizing_ = false; changed(); return true;
  }
  if (!G_VALUE_HOLDS(value, ADW_TYPE_TAB_PAGE)) return false;
  auto* page = ADW_TAB_PAGE(g_value_get_object(value));
  CefRefPtr<Browser> source = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)), "browser"));
  if (!source || source->closing || !source->owner()) return false;
  if (destination && destination->owner() != &win_) return false;
  if (!destination && !target_id.empty() && !group(target_id)) return false;
  std::string id = destination ? destination->group_id : target_id;
  const bool merge = destination && destination != source && drag_merge_;
  if (merge && id.empty()) {
    char* uuid = g_uuid_string_random(); id = uuid; g_free(uuid);
    win_.app.state.groups.push_back({id,"New group",overview_open_,""});
    destination->group_id = id;
  }
  int position = destination ? adw_tab_view_get_page_position(win_.tabs, adw_tab_view_get_page(win_.tabs, destination->widget())) : adw_tab_view_get_n_pages(win_.tabs);
  if (!destination && !id.empty()) {
    // A tab dropped onto a group joins it at the end, without scattering its members.
    std::vector<CefRefPtr<Browser>> remaining;
    for (const auto& b : all) if (b != source) remaining.push_back(b);
    for (size_t i = 0; i < remaining.size(); ++i) if (remaining[i]->group_id == id) position = i+1;
  }
  auto* origin = source->owner(); prepare_drop(source, widget, x, y); end_drag(); source->group_id = id;
  if (origin == &win_) adw_tab_view_reorder_page(win_.tabs, page, std::min(position, adw_tab_view_get_n_pages(win_.tabs)-1));
  else adw_tab_view_transfer_page(origin->tabs, page, win_.tabs, std::min(position, adw_tab_view_get_n_pages(win_.tabs)));
  if (origin != &win_) { activate(source); gtk_window_present(win_.window); }
  changed(); return true;
}
void WorkspaceTabs::rebuild_strip() {
  if (header_collapse_) { gtk_box_remove(GTK_BOX(strip_),header_collapse_); header_collapse_ = nullptr; }
  clear(strip_contents_); strip_tabs_.clear(); group_buttons_.clear(); strip_groups_.clear(); std::set<std::string> displayed; GtkWidget* selected = nullptr;
  auto all = pages();
  std::string expanded_header;
  for (const auto& browser : all) if (auto* g = group(browser->group_id); g && !g->collapsed) { expanded_header = g->id; break; }
  update_colors(expanded_header);
  g_object_set_data(G_OBJECT(strip_contents_), "tab-count", GINT_TO_POINTER(all.size()));
  auto shape_pages = all; std::sort(shape_pages.begin(), shape_pages.end(), [](const auto& a, const auto& b) { return std::less<Browser*>{}(a.get(), b.get()); });
  std::ostringstream shape;
  for (const auto& browser : shape_pages) {
    shape << browser.get() << browser->group_id;
    if (auto* g = group(browser->group_id)) shape << g->collapsed << g->title;
  }
  g_object_set_data_full(G_OBJECT(strip_contents_), "tab-shape", g_strdup(shape.str().c_str()), g_free);
  for (auto& browser : all) {
    if (!expanded_header.empty() && browser->group_id != expanded_header) continue;
    auto* g = group(browser->group_id);
    if (!g) {
      auto* child = tab(browser, false);
      g_object_set_data_full(G_OBJECT(child), "layout-key", g_strdup_printf("tab-%p", browser.get()), g_free);
      gtk_widget_set_parent(child, strip_contents_);
      if (gtk_widget_has_css_class(child, "selected")) selected = child;
      continue;
    }
    if (!displayed.insert(g->id).second) continue;
    const auto id = g->id;
    GtkWidget* container;
    if (g->collapsed) {
      container = gtk_button_new(); gtk_widget_add_css_class(container, "flat");
      GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
      gtk_box_append(GTK_BOX(content), label(g->title)); gtk_box_append(GTK_BOX(content), gtk_image_new_from_icon_name("go-next-symbolic"));
      gtk_button_set_child(GTK_BUTTON(container), content);
      gtk_accessible_update_state(GTK_ACCESSIBLE(container), GTK_ACCESSIBLE_STATE_EXPANDED, false, -1);
      g_signal_connect(container, "clicked", G_CALLBACK(+[](GtkButton*, Target* data) { auto id = data->id; data->ui->toggle_group(id); }), target(container, this, nullptr, id));
      group_buttons_[id] = container; attach_drag(container, container, nullptr, id);
    } else {
      container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_widget_add_css_class(container,"expanded");
      GtkWidget* collapse = button("go-previous-symbolic","Collapse Tab Group"); gtk_widget_add_css_class(collapse,"group-collapse");
      gtk_widget_set_valign(collapse,GTK_ALIGN_CENTER); gtk_widget_set_size_request(collapse,34,34);
      g_signal_connect(collapse,"clicked",G_CALLBACK(+[](GtkButton*,Target* data) { const auto id = data->id; data->ui->toggle_group(id); }),target(collapse,this,nullptr,id));
      // Keep the group control outside the scrolling members so it remains
      // reachable when the group contains more tabs than the header can show.
      gtk_box_prepend(GTK_BOX(strip_),collapse); header_collapse_ = collapse; group_buttons_[id] = collapse; attach_drag(collapse,container,nullptr,id);
      auto* context = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context),3);
      g_signal_connect(context,"pressed",G_CALLBACK(+[](GtkGestureClick* gesture,int,double x,double y,Target* data) {
        gtk_gesture_set_state(GTK_GESTURE(gesture),GTK_EVENT_SEQUENCE_CLAIMED);
        data->ui->group_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)),data->id,x,y);
      }),target(collapse,this,nullptr,id)); gtk_widget_add_controller(collapse,GTK_EVENT_CONTROLLER(context));
      auto* members = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_set_homogeneous(GTK_BOX(members),true); gtk_widget_set_hexpand(members,true);
      for (auto& member : all) if (member->group_id == id) gtk_box_append(GTK_BOX(members),tab(member,false));
      gtk_box_append(GTK_BOX(container),members);
    }
    gtk_widget_add_css_class(container,group_class(id).c_str());
    gtk_widget_add_css_class(container, "tab-group"); gtk_widget_set_valign(container, GTK_ALIGN_CENTER); gtk_widget_set_hexpand(container, true);
    gtk_widget_set_tooltip_text(container, g->title.c_str());
    if (auto current = win_.current(); current && !current->is_home && current->group_id == id) {
      gtk_widget_add_css_class(container, "active-group"); selected = g->collapsed ? container : strip_tab(current.get());
    }
    strip_groups_[id] = container;
    if (dropping_group_ == id && !dropping_in_overview_) gtk_widget_add_css_class(container, "drag-placeholder");
    group_drop(container, id);
    auto* menu = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), 3);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(menu), GTK_PHASE_CAPTURE);
    g_signal_connect(menu, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, Target* data) {
      auto* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
      for (auto* picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT); picked && picked != widget; picked = gtk_widget_get_parent(picked))
        if (gtk_widget_has_css_class(picked, "document-tab")) return;
      gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
      data->ui->group_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)), data->id, x, y);
    }), target(container, this, nullptr, id)); gtk_widget_add_controller(container, GTK_EVENT_CONTROLLER(menu));
    g_object_set_data_full(G_OBJECT(container), "layout-key", g_strdup(id.c_str()), g_free);
    gtk_widget_set_parent(container, strip_contents_);
  }
  if (selected) gtk_widget_add_tick_callback(selected, +[](GtkWidget* widget, GdkFrameClock*, gpointer p) -> gboolean {
    auto* self = static_cast<WorkspaceTabs*>(p); graphene_rect_t bounds;
    if (self->win_.closing || self->win_.destroyed) return G_SOURCE_REMOVE;
    if (gtk_widget_get_width(widget) <= 0 || gtk_widget_get_height(widget) <= 0) return G_SOURCE_CONTINUE;
    if (self->scroll_refresh_ || reinterpret_cast<SelectTabStrip*>(self->strip_contents_)->layout->progress < 1) return G_SOURCE_CONTINUE;
    if (!gtk_widget_compute_bounds(widget, self->strip_contents_, &bounds)) return G_SOURCE_REMOVE;
    auto* scroll = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->strip_scroll_)); const double value = gtk_adjustment_get_value(scroll), width = gtk_adjustment_get_page_size(scroll);
    double next = value;
    if (bounds.origin.x < value) next = bounds.origin.x;
    else if (bounds.origin.x+bounds.size.width > value+width) next = bounds.origin.x+bounds.size.width-width;
    gtk_adjustment_set_value(scroll, std::clamp(next, gtk_adjustment_get_lower(scroll), std::max(gtk_adjustment_get_lower(scroll), gtk_adjustment_get_upper(scroll)-width)));
    return G_SOURCE_REMOVE;
  }, this, nullptr);
}
GtkWidget* WorkspaceTabs::create_pages(GtkWidget* content) {
  pages_host_ = GTK_WIDGET(g_object_new(select_page_slide_get_type(), nullptr));
  auto* slide = reinterpret_cast<SelectPageSlide*>(pages_host_);
  slide->child = content; gtk_widget_set_parent(slide->child, pages_host_);
  return pages_host_;
}
double WorkspaceTabs::page_transition_offset() const {
  auto* slide = reinterpret_cast<SelectPageSlide*>(pages_host_);
  return slide && slide->outgoing ? slide->direction*(1-slide->progress) : 0;
}
GtkWidget* WorkspaceTabs::create_root(GtkWidget* workspace) {
  root_ = GTK_WIDGET(g_object_new(select_overview_host_get_type(), nullptr));
  auto* host = reinterpret_cast<SelectOverviewHost*>(root_); host->workspace = workspace; host->overview = overview_;
  attach_window_drop(root_);
  // Like AdwTabOverview's child_bin: include the window background in the
  // shrinking workspace, so the overview header cannot show through its chrome.
  gtk_widget_add_css_class(workspace, "background");
  gtk_widget_set_parent(workspace, root_); gtk_widget_set_parent(overview_, root_); gtk_widget_set_child_visible(overview_, false);
  g_signal_connect(root_, "drop-done", G_CALLBACK(+[](GtkWidget*, WorkspaceTabs* self) {
    self->settling_ = false; self->dropping_browser_ = nullptr; self->dropping_group_.clear();
    if (!self->win_.destroyed && !self->win_.closing) { self->signature_.clear(); self->update(); }
  }), this);
  g_signal_connect(root_, "transition-done", G_CALLBACK(+[](GtkWidget*, WorkspaceTabs* self) {
    if (self->pending_activation_ && !self->win_.destroyed && !self->win_.closing) {
      auto browser = self->pending_activation_;
      if (browser->is_home) { self->win_.native_home(); if (auto current = self->win_.current()) gtk_widget_grab_focus(current->surface()); }
      else self->activate(browser);
      self->pending_activation_ = nullptr;
    }
  }), this);
  g_signal_connect(root_, "layout-changed", G_CALLBACK(+[](GtkWidget*, int width, WorkspaceTabs* self) {
    if (self->overview_open_ && width != self->layout_width_) { self->layout_width_ = width; self->rebuild_overview(); }
  }), this);
  return root_;
}
void WorkspaceTabs::show_overview(bool visible) {
  if (!root_ || visible == overview_open_) return;
  if (pages_host_) adw_animation_skip(reinterpret_cast<SelectPageSlide*>(pages_host_)->animation);
  overview_open_ = visible;
  auto* host = reinterpret_cast<SelectOverviewHost*>(root_); host->open = visible; host->animating = true;
  gtk_widget_set_child_visible(overview_, true); gtk_widget_set_can_target(overview_, visible); gtk_widget_set_can_focus(overview_, visible);
  gtk_widget_set_can_target(host->workspace, false); gtk_widget_set_can_focus(host->workspace, !visible);
  if (visible) { pending_activation_ = nullptr; g_clear_object(&host->activation_image); layout_width_ = gtk_widget_get_width(root_); rebuild_overview(); }
  auto browser = pending_activation_ ? pending_activation_ : win_.current(); GtkWidget* thumbnail = nullptr;
  if (zoom_from_new_tab_ && !visible) thumbnail = new_tab_button_;
  else if (browser && browser->is_home) thumbnail = home_card_;
  else if (browser) {
    if (auto found = previews_.find(browser.get()); found != previews_.end()) thumbnail = found->second;
    else if (auto found = overview_groups_.find(browser->group_id); found != overview_groups_.end()) thumbnail = found->second;
  }
  g_set_weak_pointer(&host->thumbnail, thumbnail); g_set_weak_pointer(&host->page, pending_activation_ ? win_.content_ : browser ? browser->widget() : nullptr);
  host->group_target = thumbnail && (gtk_widget_has_css_class(thumbnail, "group-card") || thumbnail == home_card_);
  zoom_from_new_tab_ = false;
  adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(host->animation), host->progress);
  adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(host->animation), visible ? 1 : 0);
  adw_animation_play(host->animation);
  if (visible) gtk_widget_child_focus(overview_contents_, GTK_DIR_TAB_FORWARD);
  else { gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(search_bar_), false); if (browser && !pending_activation_) gtk_widget_grab_focus(browser->surface()); }
}
GtkWidget* WorkspaceTabs::strip_tab(Browser* browser) const {
  auto found = strip_tabs_.find(browser); return found == strip_tabs_.end() ? nullptr : found->second;
}
double WorkspaceTabs::drop_progress() const { return reinterpret_cast<SelectOverviewHost*>(root_)->drop_progress; }
GtkWidget* WorkspaceTabs::transition_origin() const { return reinterpret_cast<SelectOverviewHost*>(root_)->thumbnail; }
void WorkspaceTabs::prepare_drop(CefRefPtr<Browser> browser, GtkWidget* widget, double x, double y, const std::string& id) {
  if (!browser || !browser->owner() || !root_) return;
  auto* origin = browser->owner()->workspace.get(); if (!origin->drag_image_) return;
  auto* host = reinterpret_cast<SelectOverviewHost*>(root_);
  if (host->drop_image) adw_animation_skip(host->drop_animation);
  graphene_point_t point = GRAPHENE_POINT_INIT(static_cast<float>(x), static_cast<float>(y)); graphene_point_t root_point;
  if (!gtk_widget_compute_point(widget, root_, &point, &root_point)) return;
  g_set_object(&host->drop_image, origin->drag_image_); host->drop_progress = 0;
  graphene_rect_init(&host->drop_from, root_point.x-origin->drag_hotspot_x_, root_point.y-origin->drag_hotspot_y_, gdk_paintable_get_intrinsic_width(origin->drag_image_), gdk_paintable_get_intrinsic_height(origin->drag_image_));
  host->drop_to = host->drop_from; dropping_group_ = id; dropping_browser_ = browser; dropping_in_overview_ = overview_open_;
  if (!overview_open_) g_object_set_data(G_OBJECT(strip_contents_), "drop-layout", GINT_TO_POINTER(1));
  gtk_widget_add_tick_callback(root_, +[](GtkWidget* widget, GdkFrameClock*, gpointer p) -> gboolean {
    auto* self = static_cast<WorkspaceTabs*>(p); auto* host = reinterpret_cast<SelectOverviewHost*>(widget);
    if (self->settling_ || !self->dropping_browser_ || !host->drop_image || self->win_.destroyed) return G_SOURCE_REMOVE;
    GtkWidget* main = self->preview_button(self->dropping_browser_.get());
    GtkWidget* tile = self->dropping_in_overview_ ? (main ? gtk_widget_get_ancestor(main, GTK_TYPE_BOX) : nullptr) : self->strip_tab(self->dropping_browser_.get());
    if (!self->dropping_group_.empty()) {
      const auto found = self->strip_groups_.find(self->dropping_group_);
      tile = self->dropping_in_overview_ ? self->overview_group(self->dropping_group_) : (found == self->strip_groups_.end() ? nullptr : found->second);
    }
    if (!tile && !self->dropping_browser_->group_id.empty()) {
      tile = self->dropping_in_overview_ ? self->overview_group(self->dropping_browser_->group_id) : self->group_button(self->dropping_browser_->group_id);
    }
    if (!tile || self->dropping_browser_->closing) { adw_animation_skip(host->drop_animation); return G_SOURCE_REMOVE; }
    if (!gtk_widget_get_width(tile)) return G_SOURCE_CONTINUE;
    if (!gtk_widget_compute_bounds(tile, widget, &host->drop_to)) { adw_animation_skip(host->drop_animation); return G_SOURCE_REMOVE; }
    g_set_weak_pointer(&host->drop_target, tile); gtk_widget_add_css_class(tile, "drag-placeholder"); gtk_widget_set_can_target(tile, false);
    self->settling_ = true;
    adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(host->drop_animation), 0); adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(host->drop_animation), 1);
    adw_animation_play(host->drop_animation); return G_SOURCE_REMOVE;
  }, this, nullptr);
}
void WorkspaceTabs::attach_reorder_drop(GtkWidget* widget) {
  auto* reorder = gtk_drop_target_new(ADW_TYPE_TAB_PAGE, GDK_ACTION_MOVE); gtk_drop_target_set_preload(reorder, true);
  GType types[] = {ADW_TYPE_TAB_PAGE, select_group_drag_get_type()}; gtk_drop_target_set_gtypes(reorder, types, 2);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(reorder), GTK_PHASE_CAPTURE);
  g_signal_connect(reorder, "motion", G_CALLBACK((+[](GtkDropTarget* controller, double x, double y, WorkspaceTabs* self) -> GdkDragAction {
    if (!self->drag_tile_ || self->drag_positions_.empty()) return static_cast<GdkDragAction>(0);
    auto* reference = self->drag_in_overview_ ? self->overview_contents_ : self->strip_contents_;
    graphene_point_t point = GRAPHENE_POINT_INIT(static_cast<float>(x),static_cast<float>(y)), mapped;
    if (!gtk_widget_compute_point(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)),reference,&point,&mapped)) return static_cast<GdkDragAction>(0);
    x = mapped.x; y = mapped.y; self->drag_ungroup_ = false;
    GtkWidget* closest = nullptr; double distance = G_MAXDOUBLE;
    for (const auto& item : self->drag_positions_) {
      const double dx = x-item.point.x-gtk_widget_get_width(item.tile)/2; const double dy = y-item.point.y-gtk_widget_get_height(item.tile)/2;
      const double next = dx*dx+dy*dy; if (next < distance) { distance = next; closest = item.tile; }
    }
    // Drawer members can move into another group or out to the main grid.
    // Keep fixed drag coordinates for siblings; only hit-test other scopes.
    if (self->drag_in_overview_) {
      auto* widget = reference; bool hit_external = false;
      auto external = [&](GtkWidget* tile) {
        if (!tile || !gtk_widget_get_mapped(tile) || std::any_of(self->drag_positions_.begin(), self->drag_positions_.end(), [&](const auto& item) { return item.tile == tile; })) return;
        graphene_rect_t bounds; graphene_point_t point = GRAPHENE_POINT_INIT(static_cast<float>(x),static_cast<float>(y));
        if (gtk_widget_compute_bounds(tile, widget, &bounds) && graphene_rect_contains_point(&bounds, &point)) { closest = tile; hit_external = true; }
      };
      for (auto* tile : self->overview_tiles_) external(tile);
      for (const auto& [browser, main] : self->preview_buttons_) external(gtk_widget_get_ancestor(main, GTK_TYPE_BOX));
      auto* source = static_cast<Target*>(g_object_get_data(G_OBJECT(self->drag_tile_),"target"));
      if (!hit_external && source && source->browser && !source->browser->group_id.empty() && self->drawer_) {
        graphene_rect_t panel;
        if (gtk_widget_compute_bounds(self->drawer_,reference,&panel) && !graphene_rect_contains_point(&panel,&mapped)) { self->drag_ungroup_ = true; closest = nullptr; }
      }
    }
    auto* from = static_cast<Target*>(g_object_get_data(G_OBJECT(self->drag_tile_),"target"));
    auto* to = closest ? static_cast<Target*>(g_object_get_data(G_OBJECT(closest),"target")) : nullptr;
    bool center = false;
    if (from && from->browser && to && to->browser && from->browser != to->browser && (to->browser->group_id.empty() || from->browser->group_id != to->browser->group_id)) {
      // Use the fixed source slots, so a moving sibling cannot steal the
      // grouping target. The middle groups; the edges reorder immediately.
      graphene_rect_t bounds;
      const auto slot = std::find_if(self->drag_positions_.begin(),self->drag_positions_.end(),[&](const auto& item) { return item.tile == closest; });
      if (slot != self->drag_positions_.end()) graphene_rect_init(&bounds,slot->point.x,slot->point.y,gtk_widget_get_width(closest),gtk_widget_get_height(closest));
      else if (!gtk_widget_compute_bounds(closest,reference,&bounds)) bounds = GRAPHENE_RECT_INIT(0,0,0,0);
      center = x > bounds.origin.x+bounds.size.width*.25 && x < bounds.origin.x+bounds.size.width*.75 && y >= bounds.origin.y && y <= bounds.origin.y+bounds.size.height;
    }
    GtkWidget* candidate = center ? closest : nullptr;
    if (candidate != self->drag_merge_) {
      if (self->drag_merge_) gtk_widget_remove_css_class(self->drag_merge_,"group-drop-candidate");
      self->drag_merge_ = candidate;
      if (candidate) gtk_widget_add_css_class(candidate,"group-drop-candidate");
    }
    if (candidate) { self->hover_drag(nullptr); self->drag_hover_ = candidate; }
    else self->hover_drag(closest);
    return GDK_ACTION_MOVE;
  })), this);
  g_signal_connect(reorder, "drop", G_CALLBACK(+[](GtkDropTarget* controller, const GValue* value, double x, double y, WorkspaceTabs* self) -> gboolean {
    if (self->drag_ungroup_ && G_VALUE_HOLDS(value,ADW_TYPE_TAB_PAGE)) return self->drop_on(value,gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)),x,y,nullptr,"");
    auto* destination = self->drag_hover_ ? static_cast<Target*>(g_object_get_data(G_OBJECT(self->drag_hover_), "target")) : nullptr;
    if (!destination) return false;
    return self->drop_on(value, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), x, y, destination->browser, destination->id);
  }), this); gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(reorder));
}
void WorkspaceTabs::receive(CefRefPtr<Browser> browser, const std::string& id, Window& origin, int position) {
  if (!browser || browser->closing || &origin == &win_) return;
  auto* source = origin.workspace.get(); std::vector<CefRefPtr<Browser>> members;
  if (id.empty()) { browser->group_id.clear(); members.push_back(browser); }
  else for (const auto& member : source->pages()) if (member->group_id == id && !member->closing) members.push_back(member);
  if (members.empty()) return;
  auto preferred = browser;
  if (!id.empty()) {
    auto remembered = source->last_group_tabs_.find(id);
    for (const auto& member : members) if (remembered != source->last_group_tabs_.end() && remembered->second == member.get()) preferred = member;
    last_group_tabs_[id] = preferred.get();
  }
  source->normalizing_ = normalizing_ = true;
  for (const auto& member : members) adw_tab_view_transfer_page(origin.tabs,adw_tab_view_get_page(origin.tabs,member->widget()),win_.tabs,position++);
  source->normalizing_ = normalizing_ = false; source->end_drag();
  changed(); activate(preferred); gtk_window_present(win_.window);
}
void WorkspaceTabs::detach(CefRefPtr<Browser> browser, const std::string& id) {
  if (!browser || browser->closing || browser->owner() != &win_) return;
  end_drag(); auto* other = win_.app.new_window(false); other->workspace->receive(browser,id,win_,0);
}
void WorkspaceTabs::attach_window_drop(GtkWidget* widget) {
  auto* drop = gtk_drop_target_new(ADW_TYPE_TAB_PAGE,GDK_ACTION_MOVE); GType types[] = {ADW_TYPE_TAB_PAGE,select_group_drag_get_type()};
  gtk_drop_target_set_gtypes(drop,types,2); gtk_drop_target_set_preload(drop,true);
  g_signal_connect(drop,"motion",G_CALLBACK(+[](GtkDropTarget* drop,double,double,WorkspaceTabs* self) -> GdkDragAction {
    const auto* value = gtk_drop_target_get_value(drop); if (!value) return GDK_ACTION_MOVE;
    if (G_VALUE_HOLDS(value,ADW_TYPE_TAB_PAGE)) {
      auto* page = ADW_TAB_PAGE(g_value_get_object(value)); auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)),"browser"));
      return browser && browser->owner() != &self->win_ ? GDK_ACTION_MOVE : static_cast<GdkDragAction>(0);
    }
    auto* payload = static_cast<GroupDragPayload*>(g_value_get_boxed(value)); return payload && payload->origin != self ? GDK_ACTION_MOVE : static_cast<GdkDragAction>(0);
  }),this);
  g_signal_connect(drop,"drop",G_CALLBACK(+[](GtkDropTarget* drop,const GValue* value,double x,double y,WorkspaceTabs* self) -> gboolean {
    if (G_VALUE_HOLDS(value,ADW_TYPE_TAB_PAGE)) {
      auto* page = ADW_TAB_PAGE(g_value_get_object(value)); auto* browser = static_cast<Browser*>(g_object_get_data(G_OBJECT(adw_tab_page_get_child(page)),"browser"));
      if (!browser || browser->owner() == &self->win_) return false;
    } else {
      auto* payload = static_cast<GroupDragPayload*>(g_value_get_boxed(value)); if (!payload || payload->origin == self) return false;
    }
    auto* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop));
    auto* picked = gtk_widget_pick(widget,x,y,GTK_PICK_DEFAULT); Target* destination = nullptr;
    for (;picked && picked != widget;picked = gtk_widget_get_parent(picked)) if ((destination = static_cast<Target*>(g_object_get_data(G_OBJECT(picked),"target")))) break;
    return self->drop_on(value,widget,x,y,destination ? destination->browser : nullptr,destination ? destination->id : "");
  }),this); gtk_widget_add_controller(widget,GTK_EVENT_CONTROLLER(drop));
}
GtkWidget* WorkspaceTabs::preview_button(Browser* browser) const {
  auto found = preview_buttons_.find(browser); return found == preview_buttons_.end() ? nullptr : found->second;
}
double WorkspaceTabs::transition_progress() const { return reinterpret_cast<SelectOverviewHost*>(root_)->progress; }
GtkWidget* WorkspaceTabs::overview_group(const std::string& id) const {
  auto found = overview_groups_.find(id); return found == overview_groups_.end() ? nullptr : found->second;
}
void WorkspaceTabs::dim_overview() {
  if (home_card_) { if (expanded_.empty()) gtk_widget_remove_css_class(home_card_, "dimmed"); else gtk_widget_add_css_class(home_card_, "dimmed"); }
  for (auto* tile : overview_tiles_) {
    auto* data = static_cast<Target*>(g_object_get_data(G_OBJECT(tile), "target"));
    if (!expanded_.empty() && (!data || data->id != expanded_)) gtk_widget_add_css_class(tile, "dimmed");
    else gtk_widget_remove_css_class(tile, "dimmed");
    if (GTK_IS_BUTTON(tile) && data && !data->id.empty()) gtk_accessible_update_state(GTK_ACCESSIBLE(tile), GTK_ACCESSIBLE_STATE_EXPANDED, data->id == expanded_, -1);
  }
}
void WorkspaceTabs::expand_overview(const std::string& id) {
  if (!id.empty() && !group(id)) return;
  if (id.empty() && drawer_) {
    expanded_.clear(); drawer_set_open(drawer_, false); dim_overview(); return;
  }
  if (!id.empty() && drawer_ && id == drawer_id_) { expanded_ = id; dim_overview(); drawer_set_open(drawer_, true); return; }
  expanded_ = id; rebuild_overview();
  if (drawer_) drawer_set_open(drawer_, true);
}

void WorkspaceTabs::rebuild_overview() {
  const double previous_progress = drawer_ ? reinterpret_cast<SelectGroupDrawer*>(drawer_)->progress : 0;
  const std::string previous_id = drawer_id_;
  const std::string visible_drawer = expanded_.empty() && previous_progress > 0 ? previous_id : expanded_;
  end_drag(); clear(overview_contents_); home_card_ = nullptr; drawer_ = nullptr; drawer_id_.clear(); overview_tiles_.clear(); previews_.clear(); preview_buttons_.clear(); overview_groups_.clear();
  adw_window_title_set_title(overview_title_, (std::to_string(adw_tab_view_get_n_pages(win_.tabs)) + (adw_tab_view_get_n_pages(win_.tabs) == 1 ? " Tab" : " Tabs")).c_str());
  struct Node { std::string id; CefRefPtr<Browser> browser; };
  std::vector<Node> nodes; auto all = pages(); std::set<std::string> seen;
  for (auto& browser : all) {
    auto* g = group(browser->group_id);
    if (g) {
      if (!seen.insert(g->id).second) continue;
      const bool matches = query_.empty() || folded(g->title).find(query_) != std::string::npos ||
        std::any_of(all.begin(), all.end(), [&](const auto& b) { return b->group_id == g->id && folded(b->name).find(query_) != std::string::npos; });
      if (matches) nodes.push_back({g->id, nullptr});
    } else if (query_.empty() || folded(browser->name).find(query_) != std::string::npos) nodes.push_back({"", browser});
  }
  const double width = std::max(360, layout_width_);
  const double t = adw_easing_ease(ADW_EASE_OUT_CUBIC, std::clamp((width-360)/2200, 0., 1.));
  const double natural = adw_lerp(200, 360, t);
  const int count = std::max(1, static_cast<int>(nodes.size()));
  const int columns = std::clamp(static_cast<int>(std::ceil(width/natural)), std::min(2, count), std::min(8, count));
  const double max_columns = std::clamp(std::ceil(width/natural), 2., 8.);
  const double total = width * adw_lerp(1, .85, t) * (.5+.5*columns/max_columns);
  tile_width_ = std::clamp(static_cast<int>(std::ceil((total-5*(columns+1))/columns)), 100, 500); columns_ = columns;
  home_card_ = gtk_button_new(); gtk_widget_add_css_class(home_card_, "overview-home"); gtk_widget_add_css_class(home_card_, "card"); gtk_widget_add_css_class(home_card_, "flat");
  GtkWidget* home_content = adw_button_content_new(); adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(home_content), "go-home-symbolic");
  adw_button_content_set_label(ADW_BUTTON_CONTENT(home_content), "Home"); gtk_button_set_child(GTK_BUTTON(home_card_), home_content);
  gtk_widget_set_size_request(home_card_, tile_width_*columns+5*(columns-1)-12, 48); gtk_widget_set_halign(home_card_, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(home_card_, 6); gtk_widget_set_margin_bottom(home_card_, 12);
  gtk_accessible_update_property(GTK_ACCESSIBLE(home_card_), GTK_ACCESSIBLE_PROPERTY_LABEL, "Home", GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Open Home", -1);
  if (auto browser = win_.current(); browser && browser->is_home) gtk_widget_add_css_class(home_card_, "active-home");
  g_signal_connect(home_card_, "clicked", G_CALLBACK(+[](GtkButton*, WorkspaceTabs* self) {
    self->activate(self->win_.auth_browser_ ? self->win_.auth_browser_ : self->win_.home_browser_);
  }), this); gtk_box_append(GTK_BOX(overview_contents_), home_card_);
  if (nodes.empty()) {
    GtkWidget* status = adw_status_page_new(); adw_status_page_set_title(ADW_STATUS_PAGE(status), all.empty() ? "No Open Tabs" : "No Matching Tabs");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "tab-new-symbolic"); gtk_box_append(GTK_BOX(overview_contents_), status); return;
  }
  for (size_t row = 0; row < nodes.size(); row += columns) {
    GtkWidget* grid = gtk_grid_new(); gtk_grid_set_column_homogeneous(GTK_GRID(grid), true); gtk_grid_set_column_spacing(GTK_GRID(grid), 5); gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(grid, false); gtk_widget_set_valign(grid, GTK_ALIGN_START);
    int open_column = -1;
    for (int col = 0; col < columns; ++col) {
      const size_t index = row + col;
      if (index >= nodes.size()) { GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_size_request(spacer, tile_width_, -1); gtk_grid_attach(GTK_GRID(grid), spacer, col, 0, 1, 1); continue; }
      const auto& node = nodes[index]; GtkWidget* tile;
      if (node.id.empty()) tile = tab(node.browser, true);
      else {
        auto* g = group(node.id); tile = gtk_button_new(); gtk_widget_add_css_class(tile, "flat"); gtk_widget_add_css_class(tile, "group-card"); gtk_widget_add_css_class(tile,group_class(node.id).c_str());
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        std::vector<CefRefPtr<Browser>> members; for (const auto& browser : all) if (browser->group_id == node.id) members.push_back(browser);
        const auto count = members.size();
        gtk_widget_set_tooltip_text(tile, (g->title+" · "+std::to_string(count)+(count == 1 ? " tab" : " tabs")).c_str());
        GtkWidget* previews = gtk_grid_new(); gtk_grid_set_column_homogeneous(GTK_GRID(previews), true); gtk_grid_set_row_homogeneous(GTK_GRID(previews), true);
        gtk_grid_set_column_spacing(GTK_GRID(previews), 6); gtk_grid_set_row_spacing(GTK_GRID(previews), 6);
        const double ratio = std::clamp(static_cast<double>(std::max(1,gtk_widget_get_width(win_.content_)))/std::max(1,gtk_widget_get_height(win_.content_)), .8, 2.7);
        const int frame_width = tile_width_-12, frame_height = frame_width/ratio;
        const int preview_width = (frame_width-18)/2, preview_height = (frame_height-18)/2;
        gtk_widget_set_margin_start(previews, 6); gtk_widget_set_margin_end(previews, 6);
        gtk_widget_set_margin_top(previews, 6); gtk_widget_set_margin_bottom(previews, 6);
        for (size_t i = 0; i < 4; ++i) {
          auto* preview = group_preview(i < members.size() ? members[i] : nullptr, preview_width, preview_height);
          gtk_grid_attach(GTK_GRID(previews), preview, i%2, i/2, 1, 1);
        }
        auto* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class(frame, "group-preview-frame");
        gtk_widget_set_size_request(frame, frame_width, frame_height); gtk_widget_set_overflow(frame, GTK_OVERFLOW_HIDDEN);
        gtk_box_append(GTK_BOX(frame), previews); gtk_box_append(GTK_BOX(content), frame);
        auto* name = label(g->title); gtk_widget_set_margin_bottom(name, 6); gtk_box_append(GTK_BOX(content), name);
        gtk_button_set_child(GTK_BUTTON(tile), content);
        g_signal_connect(tile, "clicked", G_CALLBACK(+[](GtkButton*, Target* data) {
          auto* ui = data->ui; const auto id = data->id;
          ui->expand_overview(ui->expanded_.empty() ? id : "");
        }), target(tile, this, nullptr, node.id));
        gtk_accessible_update_state(GTK_ACCESSIBLE(tile), GTK_ACCESSIBLE_STATE_EXPANDED, expanded_ == node.id, -1);
        auto* menu = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(menu), 3);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(menu), GTK_PHASE_CAPTURE);
        g_signal_connect(menu, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, Target* data) {
          gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
          data->ui->group_menu(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)), data->id, x, y);
        }), target(tile, this, nullptr, node.id)); gtk_widget_add_controller(tile, GTK_EVENT_CONTROLLER(menu));
        attach_drag(tile, tile, nullptr, node.id);
        if (dropping_group_ == node.id && dropping_in_overview_) gtk_widget_add_css_class(tile, "drag-placeholder");
        group_drop(tile, node.id); gtk_widget_set_valign(tile, GTK_ALIGN_CENTER); overview_groups_[node.id] = tile; if (visible_drawer == node.id) open_column = col;
      }
      overview_tiles_.push_back(tile); gtk_widget_set_size_request(tile, tile_width_, -1);
      gtk_widget_set_hexpand(tile, true); gtk_grid_attach(GTK_GRID(grid), tile, col, 0, 1, 1);
    }
    gtk_box_append(GTK_BOX(overview_contents_), grid);
    if (open_column < 0) continue;
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* pointer = gtk_drawing_area_new(); gtk_widget_set_size_request(pointer, -1, 12);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(pointer), +[](GtkDrawingArea* widget, cairo_t* cr, int width, int height, gpointer data) {
      GdkRGBA bg = adw_style_manager_get_dark(adw_style_manager_get_default()) ? GdkRGBA{0.18f,0.18f,0.18f,1} : GdkRGBA{1,1,1,1};
      auto* tile = GTK_WIDGET(data); auto* ui = static_cast<Target*>(g_object_get_data(G_OBJECT(tile),"target"))->ui;
      const auto accent = ui->group_color(static_cast<Target*>(g_object_get_data(G_OBJECT(tile),"target"))->id);
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_style_context_lookup_color(gtk_widget_get_style_context(GTK_WIDGET(widget)), "popover_bg_color", &bg);
      G_GNUC_END_IGNORE_DEPRECATIONS
      graphene_rect_t bounds;
      const double x = gtk_widget_compute_bounds(tile, GTK_WIDGET(widget), &bounds) ? bounds.origin.x+bounds.size.width/2 : width/2.;
      cairo_set_source_rgb(cr, bg.red*.88+accent.red*.12, bg.green*.88+accent.green*.12, bg.blue*.88+accent.blue*.12);
      cairo_move_to(cr, x, 0); cairo_line_to(cr, x-12, height); cairo_line_to(cr, x+12, height); cairo_close_path(cr); cairo_fill(cr);
    }, g_object_ref(overview_groups_[visible_drawer]), g_object_unref);
    gtk_box_append(GTK_BOX(section), pointer);
    GtkWidget* drawer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16); gtk_widget_add_css_class(drawer, "group-drawer"); gtk_widget_add_css_class(drawer,group_class(visible_drawer).c_str());
    GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); GtkWidget* title = label(group(visible_drawer)->title);
    gtk_widget_add_css_class(title, "title-3"); gtk_widget_set_hexpand(title, true); gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(heading), title); GtkWidget* close = button("go-up-symbolic", "Collapse Tab Group");
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton*, WorkspaceTabs* self) { self->expand_overview(""); }), this);
    gtk_box_append(GTK_BOX(heading), close); gtk_box_append(GTK_BOX(drawer), heading);
    GtkWidget* members = gtk_flow_box_new(); gtk_widget_set_halign(members, GTK_ALIGN_CENTER); gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(members), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(members), true); gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(members), 1);
    const int member_count = std::max(1, static_cast<int>(std::count_if(all.begin(), all.end(), [&](const auto& browser) { return browser->group_id == visible_drawer; })));
    const int drawer_columns = std::clamp(static_cast<int>(std::ceil(width/natural)), std::min(2,member_count), std::min(8,member_count));
    const double drawer_total = width*adw_lerp(1,.85,t)*(.5+.5*drawer_columns/max_columns);
    const int outer_tile_width = tile_width_;
    tile_width_ = std::clamp(static_cast<int>((drawer_total-32-12*(drawer_columns+1))/drawer_columns),100,500);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(members), drawer_columns); gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(members), 12); gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(members), 12);
    for (auto& browser : all) if (browser->group_id == visible_drawer) gtk_flow_box_append(GTK_FLOW_BOX(members), tab(browser, true));
    tile_width_ = outer_tile_width;
    gtk_box_append(GTK_BOX(drawer), members); gtk_box_append(GTK_BOX(section), drawer);
    drawer_id_ = visible_drawer;
    drawer_ = drawer_new(section, previous_id == visible_drawer ? previous_progress : 0);
    gtk_box_append(GTK_BOX(overview_contents_), drawer_); drawer_set_open(drawer_, !expanded_.empty());
  }
  dim_overview();
}
void WorkspaceTabs::popup(GtkWidget* parent, GMenu* menu, double x, double y) {
  GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)); gtk_widget_set_parent(popover, parent);
  GdkRectangle rect{static_cast<int>(x), static_cast<int>(y), 1, 1}; gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
  g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover* popover, gpointer) {
    // GtkModelButton pops down its menu before activating the action. Preserve
    // the window action scope until that activation has completed.
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer p) -> gboolean {
      auto* popover = GTK_WIDGET(p); if (gtk_widget_get_parent(popover)) gtk_widget_unparent(popover); return G_SOURCE_REMOVE;
    }, g_object_ref(popover), g_object_unref);
  }), nullptr);
  gtk_popover_popup(GTK_POPOVER(popover)); g_object_unref(menu);
}
void WorkspaceTabs::tab_menu(GtkWidget* parent, CefRefPtr<Browser> browser, double x, double y) {
  if (browser->owner() != &win_ || browser->closing) return;
  auto* page = adw_tab_view_get_page(win_.tabs, browser->widget());
  g_signal_emit_by_name(win_.tabs, "setup-menu", page);
  GMenu* menu = g_menu_new(); g_menu_append_section(menu, nullptr, adw_tab_view_get_menu_model(win_.tabs));
  GMenu* grouping = g_menu_new(); g_menu_append(grouping, "New Group…", "win.tab-group-new");
  GMenu* move = g_menu_new();
  for (const auto& g : win_.app.state.groups) if (win_.app.group_in_use(g.id)) item(move, g.title.c_str(), "win.tab-group-move", g.id);
  if (!browser->group_id.empty()) item(move, "Remove from Group", "win.tab-group-move", "");
  if (g_menu_model_get_n_items(G_MENU_MODEL(move))) g_menu_append_submenu(grouping, "Move to Group", G_MENU_MODEL(move));
  g_object_unref(move); g_menu_append_section(menu, nullptr, G_MENU_MODEL(grouping)); g_object_unref(grouping); popup(parent, menu, x, y);
}
void WorkspaceTabs::group_menu(GtkWidget* parent, const std::string& id, double x, double y) {
  auto* g = group(id); if (!g) return;
  GMenu* menu = g_menu_new(); item(menu, "Rename Group…", "win.group-rename", id);
  item(menu,"Change Color…","win.group-color",id);
  item(menu, g->collapsed ? "Expand Group" : "Collapse Group", "win.group-toggle", id);
  item(menu, "Ungroup Tabs", "win.group-ungroup", id); popup(parent, menu, x, y);
}
