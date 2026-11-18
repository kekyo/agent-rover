// agent-rover - Startup configuration behavior tests
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
#include "command_line.h"
#include <cstdlib>
#include <iostream>

static void Require(bool condition, const char* description) {
  if (!condition) { std::cerr << description << '\n'; std::exit(1); }
}

static agent_rover::AgentCommandLineParseResult Parse(
    const std::string& ini, const std::vector<std::wstring>& cli = {}) {
  std::vector<const wchar_t*> arguments = {L"agent.exe"};
  for (const auto& argument : cli) arguments.push_back(argument.c_str());
  return agent_rover::ParseAgentConfiguration(arguments.size(), arguments.data(),
      std::vector<unsigned char>(ini.begin(), ini.end()), L"C:\\日本語 home\\.agent-rover-agent.ini");
}

static void Invalid(const std::string& ini, const std::wstring& key, size_t line) {
  const auto parsed = Parse(ini);
  Require(!parsed.ok, "Invalid INI accepted");
  Require(parsed.error.find(L".agent-rover-agent.ini:" + std::to_wstring(line)) != std::wstring::npos,
      "INI diagnostic must identify the file and line");
  Require(parsed.error.find(key) != std::wstring::npos, "INI diagnostic must identify the key");
  Require(parsed.error.find(L"secret-token") == std::wstring::npos, "Token leaked in diagnostic");
}

int main() {
  auto parsed = Parse("");
  Require(parsed.ok && parsed.options.host == "0.0.0.0" && parsed.options.port == 39397 &&
      parsed.options.auth_required && parsed.options.auth_token.empty() &&
      !parsed.options.multiple_connections && parsed.options.max_transfer_bytes == 512ull * 1024 * 1024,
      "Empty INI must preserve defaults");
  parsed = Parse("# comment\r\n [agent] \r\n host = 127.0.0.1 \r\n port=41414\r\n"
      "connection-mode=multiple\r\nmax-transfer-size=2\r\nno-auth=false\r\n"
      "unsafe-token=\"abc;#=xyz\"\r\nsafe-base-dir=\"C:\\日本語 work\\test\"\r\n");
  Require(parsed.ok && parsed.options.host == "127.0.0.1" && parsed.options.port == 41414 &&
      parsed.options.multiple_connections && parsed.options.max_transfer_bytes == 2ull * 1024 * 1024 &&
      parsed.options.auth_required && parsed.options.auth_token == "abc;#=xyz" &&
      parsed.options.safe_base_dir == L"C:\\日本語 work\\test", "INI values were not preserved");
  parsed = Parse("[agent]\nport=broken\nconnection-mode=wrong\nmax-transfer-size=-1\nhost=\n"
      "no-auth=wrong\nunsafe-token=日本語\nsafe-base-dir=\n",
      {L"--port", L"42424", L"--connection-mode", L"single", L"--max-transfer-size", L"3",
       L"--host", L"localhost", L"--unsafe-token", L"cli-token", L"--safe-base-dir", L"D:\\work"});
  Require(parsed.ok && parsed.options.port == 42424 && !parsed.options.multiple_connections &&
      parsed.options.max_transfer_bytes == 3ull * 1024 * 1024 && parsed.options.host == "localhost" &&
      parsed.options.auth_required && parsed.options.auth_token == "cli-token" &&
      parsed.options.safe_base_dir == L"D:\\work", "CLI did not override unused INI values");
  parsed = Parse("[agent]\nport=41414\nmax-transfer-size=4\n", {L"--host", L"localhost"});
  Require(parsed.ok && parsed.options.port == 41414 && parsed.options.max_transfer_bytes == 4ull * 1024 * 1024,
      "Absent CLI options must not overwrite INI");
  Require(!Parse("[agent]\nport=41414\n", {L"--port", L"invalid"}).ok,
      "Invalid CLI must not fall back to INI");

  for (const auto& token : {std::string(""), std::string("fixed")}) {
    parsed = Parse("[agent]\nno-auth=true\nunsafe-token=" + token + "\n", {L"--unsafe-token", L"cli"});
    Require(parsed.ok && parsed.options.auth_required && parsed.options.auth_token == "cli",
        "CLI token must replace the whole INI auth group");
    parsed = Parse("[agent]\nno-auth=false\nunsafe-token=" + token + "\n", {L"--no-auth"});
    Require(parsed.ok && !parsed.options.auth_required && parsed.options.auth_token.empty(),
        "CLI no-auth must replace the whole INI auth group");
  }
  Require(!Parse("", {L"--no-auth", L"--unsafe-token", L"secret-token"}).ok, "CLI auth conflict accepted");
  Require(!Parse("", {L"--unsafe-token", L"secret-token", L"--no-auth"}).ok, "Reverse CLI auth conflict accepted");
  Require(!Parse("", {L"--unsafe-token", L""}).ok, "Empty CLI token accepted");
  parsed = Parse("[agent]\nno-auth=true\nunsafe-token=\n");
  Require(parsed.ok && !parsed.options.auth_required && parsed.options.auth_token.empty(), "INI no-auth failed");
  parsed = Parse("[agent]\nno-auth=false\nunsafe-token=\n");
  Require(parsed.ok && parsed.options.auth_required && parsed.options.auth_token.empty(), "Empty INI token must generate");
  Invalid("[agent]\nno-auth=true\nunsafe-token=secret-token\n", L"unsafe-token", 3);
  Invalid("[agent]\nno-auth=1\n", L"no-auth", 2);
  Invalid("[agent]\nunsafe-token=日本語\n", L"unsafe-token", 2);
  Invalid("[agent]\nunsafe-token=" + std::string(2048, 'a') + "\n", L"unsafe-token", 2);

  for (const auto& key : {"host", "port", "connection-mode", "max-transfer-size", "no-auth", "safe-base-dir"})
    Invalid(std::string("[agent]\n") + key + "=\n", std::wstring(key, key + std::char_traits<char>::length(key)), 2);
  Invalid("port=42\n", L"port", 1);
  Invalid("[Agent]\n", L"Agent", 1);
  Invalid("[agent]\n[agent]\n", L"agent", 2);
  Invalid("[agent]\nunknown=secret-token\n", L"unknown", 2);
  Invalid("[agent]\nport=42\nport=43\n", L"port", 3);
  Invalid("[agent]\nunsafe-token=secret-token\nunsafe-token=secret-token\n", L"unsafe-token", 3);
  Invalid("[agent]\nunsafe-token secret-token\n", L"unsafe-token", 2);
  Invalid("[agent secret-token]\n", L"section", 1);
  Invalid("[agent]\nport\n", L"port", 2);
  Invalid("[agent]\nport=\"42\n", L"port", 2);
  Invalid("[agent]\nport=65536\n", L"port", 2);
  Invalid("[agent]\nport=43;comment\n", L"port", 2);
  Invalid("[agent]\nconnection-mode=Multiple\n", L"connection-mode", 2);
  Invalid("[agent]\nmax-transfer-size=0\n", L"max-transfer-size", 2);
  Require(!Parse("[agent]\nport=bad\nport=bad\n", {L"--port", L"42"}).ok,
      "CLI override must not hide structural errors");
  Require(!Parse("[agent]\nunknown=secret-token\n", {L"--no-auth"}).ok,
      "CLI override must not hide unknown keys");
  parsed = Parse("\xff\xfe\x00", {L"--help"});
  Require(parsed.ok && parsed.help_requested, "Help must ignore invalid INI");

  const std::string utf8 = "[agent]\nsafe-base-dir=C:\\日本語\\emoji-\xf0\x9f\x9a\x80\nport=42\n";
  parsed = Parse("\xef\xbb\xbf" + utf8);
  Require(parsed.ok && parsed.options.port == 42, "UTF-8 BOM was not accepted");
  const auto expected_path = parsed.options.safe_base_dir;
  const std::u16string utf16 = u"[agent]\r\nsafe-base-dir=C:\\日本語\\emoji-🚀\r\nport=42\r\n";
  std::string encoded = "\xff\xfe";
  for (const auto unit : utf16) { encoded.push_back(unit & 255); encoded.push_back(unit >> 8); }
  parsed = Parse(encoded);
  Require(parsed.ok && parsed.options.port == 42 && parsed.options.safe_base_dir == expected_path,
      "UTF-16LE Japanese / surrogate pair did not match UTF-8");
  for (const auto& invalid : {std::string("\xff\xfe\x41", 3), std::string("\xfe\xff", 2),
      std::string("\xff\xfe\x00\xd8", 4), std::string("\xff\xfe\x00\xdc", 4),
      std::string("\xff\xfe\x00\x00", 4), std::string("\xff\xfe\x00\x00\x41\x00\x00\x00", 8),
      std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
      std::string("\xf4\x90\x80\x80", 4), std::string("\xe3\x81", 2), std::string("\0", 1)})
    Require(!Parse(invalid).ok, "Invalid encoding was accepted");
  Require(Parse("[agent]\nsafe-base-dir=C:\\$HOME\\%TEMP%\\~\n").options.safe_base_dir ==
      L"C:\\$HOME\\%TEMP%\\~", "Configuration unexpectedly expanded environment variables");
  std::cout << "Configuration requirements passed\n";
}
