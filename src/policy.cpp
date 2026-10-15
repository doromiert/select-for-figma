#include "policy.hpp"
#include <glib.h>
#include <algorithm>
#include <regex>

namespace {
struct Uri {
  GUri* value;
  explicit Uri(const std::string& s) : value(g_uri_parse(s.c_str(), G_URI_FLAGS_ENCODED, nullptr)) {}
  ~Uri() { if (value) g_uri_unref(value); }
  bool https() const {
    if (!value || g_uri_get_userinfo(value)) return false;
    const char* scheme = g_uri_get_scheme(value);
    return scheme && g_ascii_strcasecmp(scheme, "https") == 0 &&
      (g_uri_get_port(value) == -1 || g_uri_get_port(value) == 443);
  }
  std::string host() const {
    if (!value || !g_uri_get_host(value)) return {};
    char* lower = g_ascii_strdown(g_uri_get_host(value), -1);
    std::string result(lower); g_free(lower); return result;
  }
  std::string path() const { return value && g_uri_get_path(value) ? g_uri_get_path(value) : ""; }
};
bool figma_host(const std::string& host) {
  return host == "figma.com" ||
    (host.size() > 10 && host.compare(host.size() - 10, 10, ".figma.com") == 0);
}
const std::regex file_path("^/(file|design|board|proto|slides|deck|site|buzz|make)/([A-Za-z0-9_-]+)(/|$)");
}

bool Policy::is_figma(const std::string& url) {
  Uri uri(url); return uri.https() && figma_host(uri.host());
}
bool Policy::is_auth(const std::string& url) {
  Uri uri(url);
  if (!uri.https()) return false;
  if (uri.host() == "accounts.google.com") return true;
  // Google's account chooser hands a form POST to these cookie-transfer
  // endpoints. Opening them as external GET links loses the form and yields 400.
  static const std::regex transfer("^/accounts/(SetSID|CheckCookie)(/|$)");
  return (uri.host()=="www.google.com" || uri.host()=="google.com") && std::regex_search(uri.path(),transfer);
}
bool Policy::is_new_tab(const std::string& url) {
  return is_figma(url) && Uri(url).path() == "/desktop_new_tab";
}
bool Policy::allow_font_loopback(const std::string& requesting_origin, const std::string& page, bool enabled) {
  if (!enabled || !is_figma(page) || !is_figma(requesting_origin)) return false;
  Uri uri(requesting_origin);
  return (uri.path().empty() || uri.path() == "/") &&
    !g_uri_get_query(uri.value) && !g_uri_get_fragment(uri.value);
}
bool Policy::is_sign_in(const std::string& url) {
  if (is_auth(url)) return true;
  if (!is_figma(url)) return false;
  Uri uri(url);
  static const std::regex paths("^/((login|signup|sso|oauth|app_auth|auth)(/|$)|api/start_google_sso(/|$))");
  return std::regex_search(uri.path(), paths);
}
bool Policy::is_file(const std::string& url) { return !file_key(url).empty(); }
bool Policy::is_http(const std::string& url) {
  Uri uri(url);
  if (!uri.value) return false;
  const char* s = g_uri_get_scheme(uri.value);
  return s && (g_ascii_strcasecmp(s, "https") == 0 || g_ascii_strcasecmp(s, "http") == 0);
}
std::string Policy::file_key(const std::string& url) {
  if (!is_figma(url)) return {};
  Uri uri(url); std::smatch match; std::string path = uri.path();
  return std::regex_search(path, match, file_path) ? match[2].str() : "";
}
std::string Policy::normalize(const std::string& url) {
  char* trimmed = g_strdup(url.c_str()); g_strstrip(trimmed);
  std::string clean(trimmed); g_free(trimmed);
  if (clean.find('\n') != std::string::npos || clean.find('\r') != std::string::npos) return {};
  if (is_figma(clean)) return clean;
  Uri uri(clean);
  if (!uri.value || !g_uri_get_scheme(uri.value) ||
      g_ascii_strcasecmp(g_uri_get_scheme(uri.value), "figma") ||
      g_uri_get_userinfo(uri.value) || g_uri_get_port(uri.value) != -1) return {};
  const std::string type = uri.host();
  static const std::regex types("^(file|design|board|proto|slides|deck|site|buzz|make)$");
  if (!std::regex_match(type, types)) return {};
  std::string result = "https://www.figma.com/" + type + uri.path();
  if (g_uri_get_query(uri.value)) result += "?" + std::string(g_uri_get_query(uri.value));
  if (g_uri_get_fragment(uri.value)) result += "#" + std::string(g_uri_get_fragment(uri.value));
  return is_file(result) ? result : "";
}
std::string Policy::authentication_return(const std::string& url) {
  Uri uri(url);
  if (!uri.value || !g_uri_get_scheme(uri.value) ||
      g_ascii_strcasecmp(g_uri_get_scheme(uri.value), "figma") ||
      uri.host() != "app_auth" || uri.path() != "/redeem" ||
      g_uri_get_userinfo(uri.value) || g_uri_get_port(uri.value) != -1 ||
      !g_uri_get_query(uri.value)) return {};
  GHashTable* query = g_uri_parse_params(g_uri_get_query(uri.value), -1, "&", G_URI_PARAMS_NONE, nullptr);
  if (!query) return {};
  const char* secret = static_cast<const char*>(g_hash_table_lookup(query, "g_secret"));
  std::string result;
  if (secret && std::regex_match(secret, std::regex("[a-zA-Z0-9_-]{1,1024}")))
    result = "https://www.figma.com/app_auth/redeem?g_secret=" + std::string(secret);
  g_hash_table_unref(query); return result;
}
std::string Policy::title(const std::string& url, const std::string& page_title) {
  static GRegex* suffix = g_regex_new("\\s*[-–—|]\\s*(Figma( Design| Slides)?|FigJam)\\s*$", G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT, nullptr);
  char* cleaned = g_regex_replace(suffix, page_title.c_str(), -1, 0, "", G_REGEX_MATCH_DEFAULT, nullptr);
  std::string name = cleaned ? cleaned : page_title; g_free(cleaned);
  if (!name.empty() && name != "Figma" && name != "FigJam" && name.rfind("Loading", 0) != 0) return name;
  if (!is_file(url)) return is_auth(url) ? "Sign In" : is_new_tab(url) ? "New File" : "Figma";
  Uri uri(url); std::string path = uri.path();
  const auto start = path.find('/', 1);
  const auto slug = start == std::string::npos ? start : path.find('/', start + 1);
  if (slug != std::string::npos && slug + 1 < path.size()) {
    char* decoded = g_uri_unescape_string(path.substr(slug + 1).c_str(), nullptr);
    if (decoded) { name = decoded; g_free(decoded); std::replace(name.begin(), name.end(), '-', ' '); }
    if (!name.empty()) return name;
  }
  return "Untitled File";
}
