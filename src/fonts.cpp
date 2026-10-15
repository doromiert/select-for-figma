// Font scanning and protocol adapted from ReSketch's FontHelper.cpp.
// Copyright © 2026 Ralsei-x64. SPDX-License-Identifier: GPL-3.0-only
#include "fonts.hpp"
#include "policy.hpp"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include <json-glib/json-glib.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace {
constexpr int port = 44950;
struct FontData {
  std::mutex mutex;
  std::set<std::string> paths;
  std::string payload;
  gint64 scanned_at = 0;
};
std::string font_name(FT_Face face, FT_UInt id, const std::string& fallback = "") {
  for (FT_UInt i = 0; i < FT_Get_Sfnt_Name_Count(face); ++i) {
    FT_SfntName name{};
    if (FT_Get_Sfnt_Name(face, i, &name) || name.name_id != id) continue;
    if (name.platform_id == 0 || (name.platform_id == 3 && name.language_id == 0x409)) {
      char* text = g_convert(reinterpret_cast<const char*>(name.string), name.string_len, "UTF-8", "UTF-16BE", nullptr, nullptr, nullptr);
      if (text) { std::string result(text); g_free(text); return result; }
    }
  }
  return fallback;
}
std::string json_text(JsonObject* object) {
  JsonNode* node = json_node_new(JSON_NODE_OBJECT); json_node_take_object(node, object);
  JsonGenerator* generator = json_generator_new(); json_generator_set_root(generator, node);
  char* data = json_generator_to_data(generator, nullptr); std::string result(data);
  g_free(data); g_object_unref(generator); json_node_free(node); return result;
}
void scan(FontData& data) {
  data.paths.clear();
  JsonObject* files = json_object_new();
  FcConfig* config = FcInitLoadConfigAndFonts(); FT_Library library{};
  if (!config || FT_Init_FreeType(&library)) { if (config) FcConfigDestroy(config); json_object_unref(files); return; }
  FcPattern* pattern = FcPatternCreate(); FcObjectSet* objects = FcObjectSetBuild(FC_FILE, nullptr);
  FcFontSet* set = FcFontList(config, pattern, objects); std::set<std::string> paths;
  if (set) {
    for (int i = 0; i < set->nfont; ++i) {
      FcChar8* file = nullptr;
      if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) == FcResultMatch) {
        std::error_code error; auto path = std::filesystem::canonical(reinterpret_cast<char*>(file), error);
        if (!error) paths.insert(path.string());
      }
    }
    FcFontSetDestroy(set);
  }
  FcObjectSetDestroy(objects); FcPatternDestroy(pattern); FcConfigDestroy(config);
  for (const auto& path : paths) {
    const auto extension = std::filesystem::path(path).extension().string();
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc" && extension != ".otc") continue;
    struct stat info{}; if (stat(path.c_str(), &info) || !S_ISREG(info.st_mode) || info.st_size > 256 * 1024 * 1024) continue;
    FT_Face face{}; if (FT_New_Face(library, path.c_str(), -1, &face)) continue;
    const int count = face->num_faces; FT_Done_Face(face); JsonArray* faces = json_array_new();
    for (int index = 0; index < count; ++index) {
      int instances = 0;
      for (int instance = 0; instance <= instances; ++instance) {
        if (FT_New_Face(library, path.c_str(), index | (instance << 16), &face)) continue;
        if (!instance) instances = face->style_flags >> 16;
        const char* ps = FT_Get_Postscript_Name(face);
        if (!ps || !FT_IS_SFNT(face)) { FT_Done_Face(face); continue; }
        auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
        int weight = os2 ? os2->usWeightClass : ((face->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400);
        int stretch = os2 ? os2->usWidthClass : 5;
        bool italic = face->style_flags & FT_STYLE_FLAG_ITALIC;
        std::string family = font_name(face, 16, font_name(face, 1, face->family_name ? face->family_name : ""));
        std::string style = font_name(face, 17, font_name(face, 2, face->style_name ? face->style_name : ""));
        JsonArray* axes = json_array_new(); FT_MM_Var* variation = nullptr;
        if (!FT_Get_MM_Var(face, &variation)) {
          if (instance && instance <= static_cast<int>(variation->num_namedstyles)) style = font_name(face, variation->namedstyle[instance - 1].strid, style);
          for (FT_UInt a = 0; a < variation->num_axis; ++a) {
            const auto& axis = variation->axis[a];
            char tag[5] = {char(axis.tag >> 24), char(axis.tag >> 16), char(axis.tag >> 8), char(axis.tag), 0};
            const double value = (instance ? variation->namedstyle[instance - 1].coords[a] : axis.def) / 65536.0;
            FT_UInt flags = 0; FT_Get_Var_Axis_Flags(variation, a, &flags);
            JsonObject* o = json_object_new();
            json_object_set_string_member(o, "tag", tag);
            json_object_set_string_member(o, "name", font_name(face, axis.strid, axis.name ? axis.name : "").c_str());
            json_object_set_double_member(o, "value", value); json_object_set_double_member(o, "min", axis.minimum / 65536.0);
            json_object_set_double_member(o, "max", axis.maximum / 65536.0); json_object_set_double_member(o, "default", axis.def / 65536.0);
            json_object_set_boolean_member(o, "hidden", flags & FT_VAR_AXIS_FLAG_HIDDEN); json_array_add_object_element(axes, o);
            if (axis.tag == FT_MAKE_TAG('w','g','h','t')) weight = static_cast<int>(value + 0.5);
            if (axis.tag == FT_MAKE_TAG('i','t','a','l')) italic = value != 0;
            if (axis.tag == FT_MAKE_TAG('s','l','n','t')) italic |= value != 0;
            if (axis.tag == FT_MAKE_TAG('w','d','t','h')) {
              const double widths[] = {50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200}; stretch = 1;
              for (int w = 1; w < 9; ++w) if (std::abs(value - widths[w]) < std::abs(value - widths[stretch - 1])) stretch = w + 1;
            }
          }
          FT_Done_MM_Var(library, variation);
        }
        JsonObject* entry = json_object_new();
        json_object_set_string_member(entry, "family", family.c_str()); json_object_set_string_member(entry, "style", style.c_str());
        json_object_set_string_member(entry, "postscript", ps); json_object_set_int_member(entry, "weight", weight);
        json_object_set_int_member(entry, "stretch", stretch); json_object_set_boolean_member(entry, "italic", italic);
        json_object_set_int_member(entry, "modified_at", info.st_mtime);
        json_object_set_boolean_member(entry, "user_installed", path.rfind(std::string(g_get_home_dir()) + "/", 0) == 0);
        if (json_array_get_length(axes)) json_object_set_array_member(entry, "variationAxes", axes); else json_array_unref(axes);
        if (!family.empty() && !style.empty()) json_array_add_object_element(faces, entry); else json_object_unref(entry);
        FT_Done_Face(face);
      }
    }
    if (json_array_get_length(faces)) { json_object_set_array_member(files, path.c_str(), faces); data.paths.insert(path); }
    else json_array_unref(faces);
  }
  FT_Done_FreeType(library);
  JsonObject* root = json_object_new(); json_object_set_object_member(root, "fontFiles", files);
  json_object_set_int_member(root, "version", 23); json_object_set_string_member(root, "package", "Select for Figma");
  data.payload = json_text(root); data.scanned_at = g_get_monotonic_time();
}
void reply(GSocketConnection* connection, int status, const std::string& body, const std::string& origin = "", const char* type = "application/json") {
  std::string response = "HTTP/1.1 " + std::to_string(status) + " Response\r\nConnection: close\r\nCache-Control: no-store\r\n";
  if (!origin.empty()) response += "Access-Control-Allow-Origin: " + origin +
      "\r\nVary: Origin\r\nAccess-Control-Allow-Methods: GET, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept\r\nAccess-Control-Allow-Private-Network: true\r\n";
  response += "Content-Type: " + std::string(type) + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  GOutputStream* stream = g_io_stream_get_output_stream(G_IO_STREAM(connection));
  g_output_stream_write_all(stream, response.data(), response.size(), nullptr, nullptr, nullptr);
  g_output_stream_write_all(stream, body.data(), body.size(), nullptr, nullptr, nullptr);
}
gboolean request(GThreadedSocketService* service, GSocketConnection* connection, GObject*, gpointer) {
  auto* data = static_cast<FontData*>(g_object_get_data(G_OBJECT(service), "font-data"));
  g_socket_set_timeout(g_socket_connection_get_socket(connection), 10);
  GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
  std::string headers; char buffer[1024];
  while (headers.find("\r\n\r\n") == std::string::npos && headers.size() < 16384) {
    const gssize n = g_input_stream_read(input, buffer, sizeof buffer, nullptr, nullptr);
    if (n <= 0) return true;
    headers.append(buffer, n);
  }
  if (headers.size() >= 16384) { reply(connection, 431, "{}"); return true; }
  std::istringstream lines(headers); std::string line, method, target, version;
  std::getline(lines, line); std::istringstream first(line); first >> method >> target >> version;
  std::map<std::string, std::string> fields;
  while (std::getline(lines, line)) {
    const auto colon = line.find(':'); if (colon == std::string::npos) continue;
    char* name = g_ascii_strdown(line.substr(0, colon).c_str(), -1);
    char* value = g_strdup(line.substr(colon + 1).c_str()); g_strstrip(value);
    fields[name] = value; g_free(name); g_free(value);
  }
  const std::string origin = fields["origin"];
  GUri* from = g_uri_parse(origin.c_str(), G_URI_FLAGS_NONE, nullptr);
  const bool allowed = from && Policy::is_figma(origin) &&
      (!g_uri_get_path(from) || !*g_uri_get_path(from) || !std::strcmp(g_uri_get_path(from), "/")) &&
      !g_uri_get_query(from) && !g_uri_get_fragment(from) && fields["host"] == "127.0.0.1:" + std::to_string(port);
  if (from) g_uri_unref(from);
  if (!allowed) { reply(connection, 403, "{}"); return true; }
  if (method == "OPTIONS") { reply(connection, 204, "", origin); return true; }
  if (method != "GET") { reply(connection, 405, "{}", origin); return true; }
  GUri* uri = g_uri_parse(("http://localhost" + target).c_str(), G_URI_FLAGS_NONE, nullptr);
  if (!uri) { reply(connection, 400, "{}", origin); return true; }
  const std::string path = g_uri_get_path(uri) ? g_uri_get_path(uri) : "";
  if (path == "/figma/version") reply(connection, 200, "{\"version\":23,\"package\":\"Select for Figma\"}", origin);
  else if (path == "/figma/font-files") {
    std::lock_guard<std::mutex> lock(data->mutex);
    if (data->payload.empty() || g_get_monotonic_time() - data->scanned_at > 30000000) scan(*data);
    reply(connection, 200, data->payload.empty() ? "{}" : data->payload, origin);
  } else if (path == "/figma/font-file") {
    GHashTable* query = g_uri_get_query(uri) ? g_uri_parse_params(g_uri_get_query(uri), -1, "&", G_URI_PARAMS_NONE, nullptr) : nullptr;
    const char* file = query ? static_cast<const char*>(g_hash_table_lookup(query, "file")) : nullptr;
    std::lock_guard<std::mutex> lock(data->mutex); std::error_code error;
    const std::string canonical = file ? std::filesystem::canonical(file, error).string() : "";
    if (!file || error || canonical != file || !data->paths.count(canonical)) reply(connection, 404, "{}", origin);
    else {
      std::ifstream stream(canonical, std::ios::binary | std::ios::ate);
      if (!stream || stream.tellg() > 256 * 1024 * 1024) reply(connection, 404, "{}", origin);
      else { const size_t size = stream.tellg(); stream.seekg(0); std::string bytes(size, '\0'); stream.read(bytes.data(), size); reply(connection, 200, bytes, origin, "application/octet-stream"); }
    }
    if (query) g_hash_table_unref(query);
  } else reply(connection, 404, "{}", origin);
  g_uri_unref(uri); g_io_stream_close(G_IO_STREAM(connection), nullptr, nullptr); return true;
}
}
Fonts::Fonts() {
  service_ = G_SOCKET_SERVICE(g_threaded_socket_service_new(4));
  g_object_set_data_full(G_OBJECT(service_), "font-data", new FontData(), +[](gpointer p) { delete static_cast<FontData*>(p); });
  g_signal_connect(service_, "run", G_CALLBACK(request), nullptr);
  GInetAddress* address = g_inet_address_new_from_string("127.0.0.1");
  GSocketAddress* socket = g_inet_socket_address_new(address, port); g_object_unref(address);
  GError* error = nullptr;
  available_ = g_socket_listener_add_address(G_SOCKET_LISTENER(service_), socket, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, nullptr, nullptr, &error);
  g_object_unref(socket);
  if (!available_) { g_message("Local font helper: %s (an existing agent may already provide fonts)", error->message); g_error_free(error); }
  else g_socket_service_start(service_);
}
Fonts::~Fonts() { g_socket_service_stop(service_); g_socket_listener_close(G_SOCKET_LISTENER(service_)); g_object_unref(service_); }
