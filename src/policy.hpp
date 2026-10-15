#pragma once
#include <string>

namespace Policy {
constexpr const char* home_url = "https://www.figma.com/files/recents-and-sharing";
constexpr const char* new_tab_url = "https://www.figma.com/desktop_new_tab";
bool is_new_tab(const std::string& url);
bool allow_font_loopback(const std::string& requesting_origin, const std::string& page, bool enabled);
bool is_figma(const std::string& url);
bool is_auth(const std::string& url);
bool is_sign_in(const std::string& url);
bool is_file(const std::string& url);
bool is_http(const std::string& url);
std::string normalize(const std::string& url);
std::string authentication_return(const std::string& url);
std::string file_key(const std::string& url);
std::string title(const std::string& url, const std::string& page_title = "");
}
