#include "policy.hpp"
#include <glib.h>

int main(int argc, char** argv) {
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/policy/trusted-origins", +[] {
    g_assert_true(Policy::is_figma("https://www.figma.com/design/abc/Test"));
    g_assert_true(Policy::is_figma("https://FIGMA.COM:443/"));
    for (const auto* bad : {"http://figma.com/", "https://figma.com.attacker.example/", "https://evilfigma.com/", "https://figma.com@attacker.example/", "https://attacker@figma.com/", "https://figma.com:8443/", "javascript:alert(1)"}) g_assert_false(Policy::is_figma(bad));
    g_assert_true(Policy::is_auth("https://accounts.google.com/signin"));
    g_assert_true(Policy::is_auth("https://www.google.com/accounts/SetSID"));
    g_assert_true(Policy::is_sign_in("https://www.google.com/accounts/CheckCookie"));
    g_assert_false(Policy::is_auth("https://www.google.com/search?q=figma"));
    g_assert_false(Policy::is_auth("https://www.google.com.attacker.example/accounts/SetSID"));
    g_assert_false(Policy::is_auth("https://accounts.google.com.attacker.example/signin"));
  });
  g_test_add_func("/policy/deep-links", +[] {
    g_assert_true(Policy::normalize("figma://design/abc/My-file?node-id=2-3#section") == "https://www.figma.com/design/abc/My-file?node-id=2-3#section");
    g_assert_true(Policy::file_key("https://www.figma.com/slides/Abc_123/Test") == "Abc_123");
    for (const auto* bad : {"figma://design/", "figma://design/abc%2fdef", "figma://design@evil.example/abc", "figma://app_auth/redeem?g_secret=secret", "file:///etc/passwd", "https://example.com/"}) g_assert_true(Policy::normalize(bad).empty());
  });
  g_test_add_func("/policy/titles", +[] {
    g_assert_true(Policy::title("https://www.figma.com/design/abc/My-file") == "My file");
    g_assert_true(Policy::title("https://www.figma.com/design/abc/My-file", "A & B — Figma") == "A & B");
  });
  g_test_add_func("/policy/authentication-return", +[] {
    g_assert_true(Policy::is_sign_in("https://www.figma.com/api/start_google_sso?redirect=/files"));
    g_assert_false(Policy::is_sign_in("https://www.figma.com/design/auth/File"));
    g_assert_true(Policy::authentication_return("figma://app_auth/redeem?g_secret=test_token-123") == "https://www.figma.com/app_auth/redeem?g_secret=test_token-123");
    g_assert_true(Policy::authentication_return("figma://app_auth/redeem?g_secret=bad%26injected").empty());
    g_assert_true(Policy::authentication_return("figma://evil@ app_auth/redeem?g_secret=token").empty());
  });
  g_test_add_func("/policy/new-file-tab", +[] {
    g_assert_true(Policy::is_new_tab(Policy::new_tab_url));
    g_assert_true(Policy::title(Policy::new_tab_url) == "New File");
    g_assert_false(Policy::is_new_tab("https://www.figma.com/desktop_new_tab/other"));
    g_assert_false(Policy::is_new_tab("https://figma.com.attacker.example/desktop_new_tab"));
  });
  g_test_add_func("/policy/font-loopback", +[] {
    const char* page = "https://www.figma.com/design/abc/Test";
    g_assert_true(Policy::allow_font_loopback("https://www.figma.com", page, true));
    g_assert_false(Policy::allow_font_loopback("https://www.figma.com", page, false));
    for (const auto* bad : {"https://accounts.google.com", "https://figma.com.attacker.example", "http://www.figma.com", "https://www.figma.com/path", "https://www.figma.com?test"})
      g_assert_false(Policy::allow_font_loopback(bad, page, true));
    g_assert_false(Policy::allow_font_loopback("https://www.figma.com", "https://example.com", true));
  });
  return g_test_run();
}
