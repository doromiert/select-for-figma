#include "keepassxc.hpp"
#include <glib.h>
#include <sodium.h>
#include <filesystem>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {
using Object = std::unique_ptr<JsonObject, decltype(&json_object_unref)>;
Object make() { return Object(json_object_new(), json_object_unref); }
std::string value(JsonObject* object, const char* key) { return json_object_get_string_member(object, key); }
std::string base64(const unsigned char* bytes, size_t size) { char* text = g_base64_encode(bytes, size); std::string result(text); g_free(text); return result; }
std::vector<unsigned char> decode(const std::string& text) {
  gsize size; guchar* bytes = g_base64_decode(text.c_str(), &size);
  std::vector<unsigned char> result(bytes, bytes + size); g_free(bytes); return result;
}
std::string serialize(JsonObject* object) {
  JsonNode* node = json_node_new(JSON_NODE_OBJECT); json_node_set_object(node, object);
  char* text = json_to_string(node, false); std::string result(text); g_free(text); json_node_free(node); return result;
}
Object parse(const std::string& data) {
  JsonParser* parser = json_parser_new();
  if (!json_parser_load_from_data(parser, data.data(), data.size(), nullptr)) { g_object_unref(parser); throw std::runtime_error("Incomplete JSON"); }
  auto object = Object(json_object_ref(json_node_get_object(json_parser_get_root(parser))), json_object_unref);
  g_object_unref(parser); return object;
}
Object receive(int socket) {
  std::string message; char buffer[4096];
  while (message.size() < 1024 * 1024) {
    const auto size = read(socket, buffer, sizeof buffer); if (size <= 0) throw std::runtime_error("Socket closed");
    message.append(buffer, size);
    try { return parse(message); } catch (...) {}
  }
  throw std::runtime_error("Oversized message");
}
void send(int socket, JsonObject* object) {
  const auto data = serialize(object); size_t done = 0;
  // Fragment responses deliberately to exercise the client's stream parser.
  while (done < data.size()) {
    const auto size = write(socket, data.data() + done, std::min<size_t>(23, data.size() - done));
    if (size <= 0) throw std::runtime_error("Write failed"); done += size;
  }
}
struct Mock {
  std::string directory, path, error;
  int listener = -1;
  bool tamper;
  std::thread thread;
  explicit Mock(bool tampered = false) : tamper(tampered) {
    char* dir = g_dir_make_tmp("select-for-figma-kpxc-XXXXXX", nullptr); directory = dir; g_free(dir); path = directory + "/socket";
    listener = socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address{}; address.sun_family = AF_UNIX; std::strcpy(address.sun_path, path.c_str());
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) || listen(listener, 1)) throw std::runtime_error("Could not listen");
    thread = std::thread([this] { serve(); });
  }
  ~Mock() { shutdown(listener, SHUT_RDWR); if (thread.joinable()) thread.join(); close(listener); std::filesystem::remove_all(directory); }
  void serve() {
    const int client = accept(listener, nullptr, nullptr); if (client < 0) return;
    try {
      unsigned char public_key[32], secret_key[32]; crypto_box_keypair(public_key, secret_key);
      auto request = receive(client); auto client_key = decode(value(request.get(), "publicKey")); auto nonce = decode(value(request.get(), "nonce"));
      sodium_increment(nonce.data(), nonce.size()); auto reply = make();
      json_object_set_string_member(reply.get(), "action", "change-public-keys"); json_object_set_string_member(reply.get(), "success", "true");
      json_object_set_string_member(reply.get(), "publicKey", base64(public_key, sizeof public_key).c_str());
      json_object_set_string_member(reply.get(), "nonce", base64(nonce.data(), nonce.size()).c_str()); send(client, reply.get());
      std::string identification;
      for (int step = 0; step < (tamper ? 1 : 3); ++step) {
        request = receive(client); const auto action = value(request.get(), "action"); nonce = decode(value(request.get(), "nonce"));
        const auto ciphertext = decode(value(request.get(), "message"));
        std::string plain(ciphertext.size() - crypto_box_MACBYTES, '\0');
        if (crypto_box_open_easy(reinterpret_cast<unsigned char*>(plain.data()), ciphertext.data(), ciphertext.size(), nonce.data(), client_key.data(), secret_key)) throw std::runtime_error("Request did not authenticate");
        auto payload = parse(plain); reply = make();
        if (action == "associate") identification = value(payload.get(), "idKey");
        else if (action == "test-associate" && value(payload.get(), "key") != identification) throw std::runtime_error("Wrong identification");
        else if (action == "get-logins") {
          if (value(payload.get(), "url") != "https://www.figma.com/login") throw std::runtime_error("Sensitive query was not removed");
          JsonArray* array = json_array_new(); JsonObject* entry = json_object_new();
          json_object_set_string_member(entry, "name", "Test Figma"); json_object_set_string_member(entry, "login", "test@example.invalid");
          json_object_set_string_member(entry, "password", "dummy-test-password"); json_array_add_object_element(array, entry); json_object_set_array_member(reply.get(), "entries", array);
        }
        sodium_increment(nonce.data(), nonce.size());
        json_object_set_string_member(reply.get(), "nonce", base64(nonce.data(), nonce.size()).c_str());
        json_object_set_string_member(reply.get(), "success", "true"); json_object_set_string_member(reply.get(), "id", "Test Database");
        plain = serialize(reply.get()); std::vector<unsigned char> encrypted(plain.size() + crypto_box_MACBYTES);
        if (crypto_box_easy(encrypted.data(), reinterpret_cast<const unsigned char*>(plain.data()), plain.size(), nonce.data(), client_key.data(), secret_key)) throw std::runtime_error("Encryption failed");
        auto envelope = make(); json_object_set_string_member(envelope.get(), "action", action.c_str());
        json_object_set_string_member(envelope.get(), "message", base64(encrypted.data(), encrypted.size()).c_str());
        if (tamper) sodium_increment(nonce.data(), nonce.size());
        json_object_set_string_member(envelope.get(), "nonce", base64(nonce.data(), nonce.size()).c_str()); send(client, envelope.get());
      }
      sodium_memzero(secret_key, sizeof secret_key);
    } catch (const std::exception& exception) { error = exception.what(); }
    close(client);
  }
};
}
int main(int argc, char** argv) {
  g_test_init(&argc, &argv, nullptr); if (sodium_init() < 0) return 1;
  g_test_add_func("/keepassxc/encrypted-round-trip", +[] {
    Mock mock;
    { KeePassXC::Session session(mock.path); const auto association = session.associate();
      g_assert_true(association.id == "Test Database");
      const auto logins = session.logins(association, "https://www.figma.com/login?secret=do-not-forward#fragment");
      g_assert_cmpuint(logins.size(), ==, 1); g_assert_true(logins[0].username == "test@example.invalid"); g_assert_true(logins[0].password == "dummy-test-password");
    }
    mock.thread.join(); g_assert_true(mock.error.empty());
  });
  g_test_add_func("/keepassxc/reject-response-nonce", +[] {
    Mock mock(true); bool rejected = false;
    try { KeePassXC::Session session(mock.path); session.associate(); } catch (const std::exception&) { rejected = true; }
    g_assert_true(rejected); mock.thread.join(); g_assert_true(mock.error.empty());
  });
  g_test_add_func("/keepassxc/origin-boundaries", +[] {
    g_assert_true(KeePassXC::credential_url("https://accounts.google.com/signin?token=hidden") == "https://accounts.google.com/signin");
    for (const char* url : {"http://www.figma.com/login", "https://www.figma.com.evil.example/login", "https://evil@www.figma.com/login", "https://accounts.google.com.evil.example/login"}) g_assert_true(KeePassXC::credential_url(url).empty());
  });
  return g_test_run();
}
