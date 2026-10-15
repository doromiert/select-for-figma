#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace KeePassXC {
struct Association { std::string id; std::string key; };
struct Login {
  std::string name, username, password;
  ~Login();
};
struct Result { std::vector<Login> logins; std::string error; bool paired = false; };
using Callback = std::function<void(Result)>;

// An independent, encrypted protocol session. The optional socket path is for tests.
class Session {
public:
  explicit Session(const std::string& socket_path = "");
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Association associate();
  std::vector<Login> logins(const Association& association, const std::string& url);
private:
  GSocketConnection* connection_ = nullptr;
  std::array<unsigned char, 32> public_key_{}, secret_key_{}, server_key_{};
  std::string client_id_, buffered_;
  JsonObject* exchange(JsonObject* request);
  JsonObject* encrypted(JsonObject* request);
};

void pair(Callback callback);
void get_logins(const std::string& url, Callback callback);
void forget(Callback callback);
std::string credential_url(const std::string& url);
}
