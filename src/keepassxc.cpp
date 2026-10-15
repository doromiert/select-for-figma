#include "keepassxc.hpp"
#include "policy.hpp"
#include <gio/gunixsocketaddress.h>
#include <libsecret/secret.h>
#include <sodium.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr const char* app_id = "io.github.doromiert.SelectForFigma";
const SecretSchema schema = {
  "io.github.doromiert.SelectForFigma.KeePassXC", SECRET_SCHEMA_NONE,
  {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
  0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
// Read old associations once so changing the application ID preserves pairing.
constexpr const char* legacy_app_id = "io.github.doromiert.FigmaGtk";
const SecretSchema legacy_schema = {
  "io.github.doromiert.FigmaGtk.KeePassXC", SECRET_SCHEMA_NONE,
  {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
  0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
using Object = std::unique_ptr<JsonObject, decltype(&json_object_unref)>;
Object object(JsonObject* value = nullptr) { return Object(value ? value : json_object_new(), json_object_unref); }
std::string text(JsonObject* object, const char* key) {
  JsonNode* node = json_object_get_member(object, key);
  return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : "";
}
std::string serialize(JsonObject* value) {
  JsonNode* node = json_node_new(JSON_NODE_OBJECT); json_node_set_object(node, value);
  JsonGenerator* generator = json_generator_new(); json_generator_set_root(generator, node);
  char* data = json_generator_to_data(generator, nullptr); std::string result(data);
  g_free(data); g_object_unref(generator); json_node_free(node); return result;
}
struct WipeString {
  std::string& value;
  ~WipeString() { if (!value.empty()) sodium_memzero(value.data(), value.size()); }
};
JsonObject* parse(const std::string& value) {
  JsonParser* parser = json_parser_new();
  if (!json_parser_load_from_data(parser, value.data(), value.size(), nullptr)) { g_object_unref(parser); throw std::runtime_error("KeePassXC sent an invalid response"); }
  JsonNode* node = json_parser_get_root(parser);
  if (!node || !JSON_NODE_HOLDS_OBJECT(node)) { g_object_unref(parser); throw std::runtime_error("KeePassXC sent an invalid response"); }
  JsonObject* result = json_object_ref(json_node_get_object(node)); g_object_unref(parser); return result;
}
std::string base64(const unsigned char* bytes, size_t size) { char* value = g_base64_encode(bytes, size); std::string result(value); g_free(value); return result; }
std::vector<unsigned char> decode(const std::string& value) {
  std::vector<unsigned char> result(value.size()); size_t size = 0;
  if (sodium_base642bin(result.data(), result.size(), value.c_str(), value.size(), nullptr, &size, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) throw std::runtime_error("KeePassXC sent invalid encryption data");
  result.resize(size); return result;
}
bool success(JsonObject* object) {
  JsonNode* node = json_object_get_member(object, "success");
  return node && JSON_NODE_HOLDS_VALUE(node) &&
    ((json_node_get_value_type(node) == G_TYPE_STRING && text(object, "success") == "true") ||
     (json_node_get_value_type(node) == G_TYPE_BOOLEAN && json_node_get_boolean(node)));
}
std::string socket_path() {
  const std::string runtime = g_get_user_runtime_dir();
  for (const auto& path : {runtime + "/app/org.keepassxc.KeePassXC/org.keepassxc.KeePassXC.BrowserServer",
      runtime + "/org.keepassxc.KeePassXC.BrowserServer", runtime + "/kpxc_server"})
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
  throw std::runtime_error("Open KeePassXC and enable Browser Integration in its settings");
}
void check_error(JsonObject* response) {
  const auto error = text(response, "error");
  if (!error.empty()) throw std::runtime_error(error);
}
}
KeePassXC::Login::~Login() { if (!password.empty()) sodium_memzero(password.data(), password.size()); }
KeePassXC::Session::Session(const std::string& path) {
  if (sodium_init() < 0) throw std::runtime_error("Could not initialize password encryption");
  const auto target = path.empty() ? socket_path() : path;
  GSocketClient* client = g_socket_client_new(); g_socket_client_set_timeout(client, 3);
  GSocketAddress* address = g_unix_socket_address_new(target.c_str());
  GError* error = nullptr;
  connection_ = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(address), nullptr, &error);
  g_object_unref(address); g_object_unref(client);
  if (!connection_) { if (error) g_error_free(error); throw std::runtime_error("Could not connect to KeePassXC. Open it and enable Browser Integration."); }
  GSocket* socket = g_socket_connection_get_socket(connection_);
  struct ucred peer{}; socklen_t length = sizeof peer;
  if (getsockopt(g_socket_get_fd(socket), SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || peer.uid != geteuid()) {
    g_object_unref(connection_); connection_ = nullptr; throw std::runtime_error("KeePassXC connection belongs to another user");
  }
  g_socket_set_timeout(socket, 90);
  crypto_box_keypair(public_key_.data(), secret_key_.data());
  unsigned char id[24], nonce[24]; randombytes_buf(id, sizeof id); randombytes_buf(nonce, sizeof nonce);
  client_id_ = base64(id, sizeof id);
  auto request = object();
  json_object_set_string_member(request.get(), "action", "change-public-keys");
  json_object_set_string_member(request.get(), "publicKey", base64(public_key_.data(), public_key_.size()).c_str());
  json_object_set_string_member(request.get(), "nonce", base64(nonce, sizeof nonce).c_str());
  json_object_set_string_member(request.get(), "clientID", client_id_.c_str());
  try {
    auto response = object(exchange(request.get())); check_error(response.get());
    sodium_increment(nonce, sizeof nonce);
    if (!success(response.get()) || text(response.get(), "nonce") != base64(nonce, sizeof nonce)) throw std::runtime_error("KeePassXC key exchange could not be verified");
    const auto key = decode(text(response.get(), "publicKey"));
    if (key.size() != server_key_.size()) throw std::runtime_error("KeePassXC sent an invalid public key");
    std::copy(key.begin(), key.end(), server_key_.begin());
  } catch (...) { sodium_memzero(secret_key_.data(), secret_key_.size()); g_object_unref(connection_); connection_ = nullptr; throw; }
}
KeePassXC::Session::~Session() {
  sodium_memzero(secret_key_.data(), secret_key_.size());
  if (connection_) { g_io_stream_close(G_IO_STREAM(connection_), nullptr, nullptr); g_object_unref(connection_); }
}
JsonObject* KeePassXC::Session::exchange(JsonObject* request) {
  const auto data = serialize(request);
  GOutputStream* output = g_io_stream_get_output_stream(G_IO_STREAM(connection_));
  if (!g_output_stream_write_all(output, data.data(), data.size(), nullptr, nullptr, nullptr)) throw std::runtime_error("KeePassXC connection was interrupted");
  GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection_));
  for (;;) {
    // Outer protocol packets contain no nested objects; quoted braces still need to be skipped.
    bool quoted = false, escaped = false; int depth = 0; size_t end = std::string::npos;
    for (size_t i = 0; i < buffered_.size(); ++i) {
      const char c = buffered_[i];
      if (escaped) { escaped = false; continue; }
      if (quoted && c == '\\') { escaped = true; continue; }
      if (c == '"') quoted = !quoted;
      if (!quoted && c == '{') ++depth;
      if (!quoted && c == '}' && --depth == 0) { end = i + 1; break; }
    }
    if (end != std::string::npos) {
      auto response = object(parse(buffered_.substr(0, end))); buffered_.erase(0, end);
      if (text(response.get(), "action") != text(request, "action")) continue;
      return response.release();
    }
    char buffer[8192]; const gssize size = g_input_stream_read(input, buffer, sizeof buffer, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("KeePassXC did not respond. Unlock your database and try again.");
    buffered_.append(buffer, size);
    if (buffered_.size() > 1024 * 1024) throw std::runtime_error("KeePassXC response is too large");
  }
}
JsonObject* KeePassXC::Session::encrypted(JsonObject* request) {
  unsigned char nonce[crypto_box_NONCEBYTES]; randombytes_buf(nonce, sizeof nonce);
  std::string plain = serialize(request);
  WipeString wipe{plain};
  std::vector<unsigned char> ciphertext(plain.size() + crypto_box_MACBYTES);
  if (crypto_box_easy(ciphertext.data(), reinterpret_cast<const unsigned char*>(plain.data()), plain.size(), nonce, server_key_.data(), secret_key_.data()) != 0)
    throw std::runtime_error("Could not encrypt KeePassXC request");
  sodium_memzero(plain.data(), plain.size());
  auto envelope = object();
  json_object_set_string_member(envelope.get(), "action", text(request, "action").c_str());
  json_object_set_string_member(envelope.get(), "message", base64(ciphertext.data(), ciphertext.size()).c_str());
  json_object_set_string_member(envelope.get(), "nonce", base64(nonce, sizeof nonce).c_str());
  json_object_set_string_member(envelope.get(), "clientID", client_id_.c_str());
  auto response = object(exchange(envelope.get())); check_error(response.get());
  sodium_increment(nonce, sizeof nonce);
  if (text(response.get(), "nonce") != base64(nonce, sizeof nonce)) throw std::runtime_error("KeePassXC response nonce could not be verified");
  ciphertext = decode(text(response.get(), "message"));
  if (ciphertext.size() < crypto_box_MACBYTES) throw std::runtime_error("KeePassXC sent an invalid encrypted response");
  plain.resize(ciphertext.size() - crypto_box_MACBYTES);
  if (crypto_box_open_easy(reinterpret_cast<unsigned char*>(plain.data()), ciphertext.data(), ciphertext.size(), nonce, server_key_.data(), secret_key_.data()) != 0)
    throw std::runtime_error("KeePassXC response authentication failed");
  auto result = object(parse(plain)); sodium_memzero(plain.data(), plain.size());
  if (!success(result.get()) || text(result.get(), "nonce") != base64(nonce, sizeof nonce)) throw std::runtime_error("KeePassXC response could not be verified");
  return result.release();
}
KeePassXC::Association KeePassXC::Session::associate() {
  unsigned char identity[32], secret[32]; crypto_box_keypair(identity, secret); sodium_memzero(secret, sizeof secret);
  Association association; association.key = base64(identity, sizeof identity);
  auto request = object(); json_object_set_string_member(request.get(), "action", "associate");
  json_object_set_string_member(request.get(), "key", base64(public_key_.data(), public_key_.size()).c_str());
  json_object_set_string_member(request.get(), "idKey", association.key.c_str());
  auto response = object(encrypted(request.get())); association.id = text(response.get(), "id");
  if (association.id.empty()) throw std::runtime_error("KeePassXC did not approve the connection");
  return association;
}
std::string KeePassXC::credential_url(const std::string& url) {
  if (!Policy::is_figma(url) && !Policy::is_auth(url)) return {};
  GUri* uri = g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, nullptr);
  if (!uri) return {};
  char* value = g_uri_join(G_URI_FLAGS_ENCODED, "https", nullptr, g_uri_get_host(uri), -1, g_uri_get_path(uri), nullptr, nullptr);
  std::string result(value); g_free(value); g_uri_unref(uri); return result;
}
std::vector<KeePassXC::Login> KeePassXC::Session::logins(const Association& association, const std::string& url) {
  const auto site = credential_url(url);
  if (site.empty()) throw std::runtime_error("KeePassXC autofill is available on Figma and its Google sign-in pages");
  auto request = object(); json_object_set_string_member(request.get(), "action", "test-associate");
  json_object_set_string_member(request.get(), "id", association.id.c_str());
  json_object_set_string_member(request.get(), "key", association.key.c_str());
  auto approved = object(encrypted(request.get()));
  request = object(); json_object_set_string_member(request.get(), "action", "get-logins");
  json_object_set_string_member(request.get(), "url", site.c_str()); json_object_set_string_member(request.get(), "id", association.id.c_str());
  JsonArray* keys = json_array_new(); JsonObject* key = json_object_new();
  json_object_set_string_member(key, "id", association.id.c_str()); json_object_set_string_member(key, "key", association.key.c_str());
  json_array_add_object_element(keys, key); json_object_set_array_member(request.get(), "keys", keys);
  auto response = object(encrypted(request.get())); std::vector<Login> result;
  JsonNode* entries = json_object_get_member(response.get(), "entries");
  if (!entries || !JSON_NODE_HOLDS_ARRAY(entries)) return result;
  JsonArray* array = json_node_get_array(entries);
  for (unsigned i = 0; i < std::min(100u, json_array_get_length(array)); ++i) {
    JsonNode* item = json_array_get_element(array, i); if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
    JsonObject* entry = json_node_get_object(item);
    result.push_back({text(entry, "name"), text(entry, "login"), text(entry, "password")});
    // Keep passwords out of the retained JSON tree once copied to their transient UI objects.
    JsonNode* password_node = json_object_get_member(entry, "password");
    if (password_node && JSON_NODE_HOLDS_VALUE(password_node) && json_node_get_value_type(password_node) == G_TYPE_STRING) {
      const char* password = json_object_get_string_member(entry, "password");
      if (password) sodium_memzero(const_cast<char*>(password), std::strlen(password));
      json_object_set_string_member(entry, "password", "");
    }
  }
  return result;
}
namespace {
enum class Operation { Pair, Logins, Forget };
struct TaskData { Operation operation; std::string url; KeePassXC::Callback callback; };
void work(GTask* task, gpointer, gpointer task_data, GCancellable*) {
  auto* data = static_cast<TaskData*>(task_data); auto result = std::make_unique<KeePassXC::Result>();
  try {
    GError* error = nullptr;
    if (data->operation == Operation::Forget) {
      secret_password_clear_sync(&schema, nullptr, &error, "application", app_id, nullptr);
      if (error) { g_error_free(error); throw std::runtime_error("Could not remove the stored KeePassXC connection"); }
      secret_password_clear_sync(&legacy_schema, nullptr, &error, "application", legacy_app_id, nullptr);
      if (error) { g_error_free(error); throw std::runtime_error("Could not remove the earlier KeePassXC connection"); }
    } else if (data->operation == Operation::Pair) {
      KeePassXC::Session session; const auto association = session.associate();
      auto record = object(); json_object_set_string_member(record.get(), "id", association.id.c_str()); json_object_set_string_member(record.get(), "key", association.key.c_str());
      std::string value = serialize(record.get());
      const bool saved = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "Select for Figma KeePassXC Connection", value.c_str(), nullptr, &error, "application", app_id, nullptr);
      sodium_memzero(value.data(), value.size());
      if (!saved) { if (error) g_error_free(error); throw std::runtime_error("Could not save the connection in the system keyring"); }
      result->paired = true;
    } else {
      if (KeePassXC::credential_url(data->url).empty()) throw std::runtime_error("This page is not a supported sign-in page");
      std::unique_ptr<gchar, decltype(&secret_password_free)> stored(secret_password_lookup_sync(&schema, nullptr, &error, "application", app_id, nullptr), secret_password_free);
      if (!stored && !error) {
        stored.reset(secret_password_lookup_sync(&legacy_schema, nullptr, &error, "application", legacy_app_id, nullptr));
        if (stored && !secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "Select for Figma KeePassXC Connection", stored.get(), nullptr, &error, "application", app_id, nullptr)) {
          if (error) g_error_free(error);
          throw std::runtime_error("Could not migrate the KeePassXC connection in the system keyring");
        }
      }
      if (!stored) { if (error) g_error_free(error); throw std::runtime_error("Connect to KeePassXC in Preferences first"); }
      auto record = object(parse(stored.get())); stored.reset();
      const KeePassXC::Association association{text(record.get(), "id"), text(record.get(), "key")};
      if (association.id.empty() || association.key.empty()) throw std::runtime_error("Reconnect to KeePassXC in Preferences");
      KeePassXC::Session session; result->logins = session.logins(association, data->url); result->paired = true;
    }
  } catch (const std::exception& error) { result->error = error.what(); }
  g_task_return_pointer(task, result.release(), +[](gpointer p) { delete static_cast<KeePassXC::Result*>(p); });
}
void run(Operation operation, std::string url, KeePassXC::Callback callback) {
  GTask* task = g_task_new(nullptr, nullptr, +[](GObject*, GAsyncResult* async, gpointer) {
    auto* task = G_TASK(async); auto* data = static_cast<TaskData*>(g_task_get_task_data(task));
    std::unique_ptr<KeePassXC::Result> result(static_cast<KeePassXC::Result*>(g_task_propagate_pointer(task, nullptr)));
    data->callback(std::move(*result));
  }, nullptr);
  g_task_set_task_data(task, new TaskData{operation, std::move(url), std::move(callback)}, +[](gpointer p) { delete static_cast<TaskData*>(p); });
  g_task_run_in_thread(task, work); g_object_unref(task);
}
}
void KeePassXC::pair(Callback callback) { run(Operation::Pair, "", std::move(callback)); }
void KeePassXC::get_logins(const std::string& url, Callback callback) { run(Operation::Logins, url, std::move(callback)); }
void KeePassXC::forget(Callback callback) { run(Operation::Forget, "", std::move(callback)); }
