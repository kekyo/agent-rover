// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "command_line.h"

#include <array>
#include <map>
#include <limits>

#include "auth.h"

namespace agent_rover {

static AgentCommandLineOptions DefaultAgentCommandLineOptions() {
  return {"0.0.0.0", 39397, true, ""};
}

static AgentCommandLineParseResult ParseSuccess(
    const AgentCommandLineOptions& options,
    bool help_requested) {
  return {true, help_requested, options, L""};
}

static AgentCommandLineParseResult ParseFailure(
    const AgentCommandLineOptions& options,
    const std::wstring& error) {
  return {false, false, options, error};
}

static bool NeedsQuotes(const std::wstring& argument) {
  if (argument.empty()) {
    return true;
  }
  for (const wchar_t ch : argument) {
    if (ch == L'"' || ch == L' ' || ch == L'\t' || ch == L'\r' ||
        ch == L'\n') {
      return true;
    }
  }
  return false;
}

static bool NarrowAsciiStrict(
    const std::wstring& value,
    std::string* result) {
  result->clear();
  for (const wchar_t ch : value) {
    if (static_cast<uint32_t>(ch) > 0x7f) {
      return false;
    }
    result->push_back(static_cast<char>(ch));
  }
  return true;
}

static bool ParsePort(const std::wstring& value, uint16_t* port) {
  if (value.empty()) return false;
  uint32_t parsed = 0;
  for (const auto ch : value) {
    if (ch < L'0' || ch > L'9') return false;
    parsed = parsed * 10 + static_cast<uint32_t>(ch - L'0');
    if (parsed > std::numeric_limits<uint16_t>::max()) return false;
  }
  *port = static_cast<uint16_t>(parsed);
  return true;
}

static bool ReadNext(
    int argc,
    const wchar_t* const argv[],
    int index,
    const wchar_t* option_name,
    std::wstring* value,
    std::wstring* error) {
  if (index + 1 >= argc || std::wstring(argv[index + 1]).rfind(L"--", 0) == 0) {
    *error = std::wstring(L"Missing value for ") + option_name + L".";
    return false;
  }
  *value = argv[index + 1];
  return true;
}

static bool ParseTransferSize(const std::wstring& value, uint64_t* bytes) {
  constexpr auto maximum_mib = std::numeric_limits<uint64_t>::max() / kBytesPerMiB;
  uint64_t mib = 0;
  for (const auto ch : value) {
    if (ch < L'0' || ch > L'9') return false;
    const auto digit = static_cast<uint64_t>(ch - L'0');
    if (mib > (maximum_mib - digit) / 10) return false;
    mib = mib * 10 + digit;
  }
  if (!mib) return false;
  *bytes = mib * kBytesPerMiB;
  return true;
}

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
  if (!NeedsQuotes(argument)) {
    return argument;
  }

  std::wstring output;
  output.push_back(L'"');
  size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      backslashes += 1;
      continue;
    }
    if (ch == L'"') {
      output.append(backslashes * 2 + 1, L'\\');
      output.push_back(ch);
      backslashes = 0;
      continue;
    }
    output.append(backslashes, L'\\');
    backslashes = 0;
    output.push_back(ch);
  }
  output.append(backslashes * 2, L'\\');
  output.push_back(L'"');
  return output;
}

std::wstring BuildCommandLine(
    const std::wstring& executable,
    const std::vector<std::wstring>& arguments) {
  std::wstring output = QuoteCommandLineArgument(executable);
  for (const std::wstring& argument : arguments) {
    output.push_back(L' ');
    output += QuoteCommandLineArgument(argument);
  }
  return output;
}

// Keep presence and source separate from defaults so unused INI values cannot
// override CLI fields or fail value validation. Syntax is always checked first.
struct Setting {
  std::wstring value;
  size_t line = 0;
};
using Settings = std::map<std::wstring, Setting>;
static constexpr std::array<const wchar_t*, 7> kSettingNames = {
    L"host", L"port", L"connection-mode", L"max-transfer-size",
    L"no-auth", L"unsafe-token", L"safe-base-dir"};

static bool KnownSetting(const std::wstring& key) {
  for (const auto name : kSettingNames) if (key == name) return true;
  return false;
}

static std::wstring Diagnostic(const std::wstring& path, size_t line,
    const std::wstring& key, const wchar_t* reason) {
  return (line ? path + L":" + std::to_wstring(line) + L" [" + key + L"]" : L"--" + key) +
      L": " + reason;
}

static std::wstring Trim(const std::wstring& value) {
  const auto first = value.find_first_not_of(L" \t\r");
  if (first == std::wstring::npos) return {};
  return value.substr(first, value.find_last_not_of(L" \t\r") - first + 1);
}

// A malformed line can include a credential after a missing '='. Diagnostics
// name only a recognizable field, never the original line or section text.
static std::wstring DiagnosticName(const std::wstring& text, bool section) {
  for (const auto key : kSettingNames) {
    if (!section && text.rfind(key, 0) == 0) return key;
  }
  if (!text.empty() && text.size() <= 64) {
    bool identifier = true;
    for (const auto ch : text)
      if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-')) identifier = false;
    if (identifier) return text;
  }
  return section ? L"section" : L"key";
}

static void AppendScalar(uint32_t scalar, std::wstring* text) {
  if constexpr (sizeof(wchar_t) == 2) {
    if (scalar > 0xffff) {
      scalar -= 0x10000;
      text->push_back(static_cast<wchar_t>(0xd800 + (scalar >> 10)));
      text->push_back(static_cast<wchar_t>(0xdc00 + (scalar & 1023)));
      return;
    }
  }
  text->push_back(static_cast<wchar_t>(scalar));
}

static bool DecodeIni(const std::vector<unsigned char>& bytes,
    std::wstring* text, size_t* line) {
  *line = 1;
  const bool utf16 = bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
  size_t at = utf16 ? 2 :
      bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
  while (at < bytes.size()) {
    uint32_t scalar = bytes[at++];
    if (utf16) {
      if (at == bytes.size()) return false;
      scalar |= static_cast<uint32_t>(bytes[at++]) << 8;
      if (scalar >= 0xd800 && scalar <= 0xdbff) {
        if (at + 2 > bytes.size()) return false;
        const auto low = static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8);
        if (low < 0xdc00 || low > 0xdfff) return false;
        at += 2;
        scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
      } else if (scalar >= 0xdc00 && scalar <= 0xdfff) return false;
    } else if (scalar >= 0x80) {
      size_t count;
      uint32_t minimum;
      if (scalar >= 0xc2 && scalar <= 0xdf) { count = 1; minimum = 0x80; scalar &= 31; }
      else if (scalar >= 0xe0 && scalar <= 0xef) { count = 2; minimum = 0x800; scalar &= 15; }
      else if (scalar >= 0xf0 && scalar <= 0xf4) { count = 3; minimum = 0x10000; scalar &= 7; }
      else return false;
      if (at + count > bytes.size()) return false;
      for (size_t i = 0; i < count; ++i) {
        const auto next = bytes[at++];
        if ((next & 0xc0) != 0x80) return false;
        scalar = (scalar << 6) | (next & 63);
      }
      if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    }
    if (!scalar) return false;
    AppendScalar(scalar, text);
    if (scalar == '\n') ++*line;
  }
  return true;
}

static bool ParseIni(const std::vector<unsigned char>& document, const std::wstring& path,
    Settings* settings, std::wstring* error) {
  std::wstring text;
  size_t line;
  if (!DecodeIni(document, &text, &line)) {
    *error = Diagnostic(path, line, L"encoding", L"Expected valid UTF-8 or UTF-16LE with BOM; NUL is not allowed.");
    return false;
  }
  bool section_seen = false;
  size_t begin = 0;
  line = 0;
  while (begin < text.size()) {
    ++line;
    const auto end = text.find(L'\n', begin);
    const auto current = Trim(text.substr(begin, end == std::wstring::npos ? end : end - begin));
    begin = end == std::wstring::npos ? text.size() : end + 1;
    if (current.empty() || current[0] == L';' || current[0] == L'#') continue;
    if (current[0] == L'[') {
      if (current != L"[agent]" || section_seen) {
        const auto name = current.back() == L']' ? current.substr(1, current.size() - 2) : L"";
        *error = Diagnostic(path, line, DiagnosticName(name, true), L"Expected one [agent] section.");
        return false;
      }
      section_seen = true;
      continue;
    }
    const auto equal = current.find(L'=');
    const auto key = Trim(current.substr(0, equal));
    if (!section_seen || equal == std::wstring::npos || !KnownSetting(key) || settings->contains(key)) {
      *error = Diagnostic(path, line, DiagnosticName(key, false), L"Expected a known, unique key=value inside [agent].");
      return false;
    }
    auto value = Trim(current.substr(equal + 1));
    if (!value.empty() && (value.front() == L'"' || value.back() == L'"')) {
      if (value.size() < 2 || value.front() != L'"' || value.back() != L'"') {
        *error = Diagnostic(path, line, key, L"Value has an unmatched outer quote.");
        return false;
      }
      value = value.substr(1, value.size() - 2);
    }
    settings->emplace(key, Setting{std::move(value), line});
  }
  return true;
}

static bool ParseCli(int argc, const wchar_t* const argv[], Settings* settings,
    bool* help, std::wstring* error) {
  for (int index = 1; index < argc;) {
    const std::wstring argument = argv[index];
    if (argument == L"--help" || argument == L"-h") { *help = true; return true; }
    if (argument == L"--no-auth" || argument == L"-n") {
      (*settings)[L"no-auth"] = {L"true", 0};
      ++index;
    } else {
      const auto key = argument.rfind(L"--", 0) == 0 ? argument.substr(2) : argument;
      if (!KnownSetting(key) || argument.rfind(L"--", 0) != 0) {
        *error = L"Unknown argument: " + argument + L".";
        return false;
      }
      if (key == L"unsafe-token" && settings->contains(key)) {
        *error = L"--unsafe-token was specified twice.";
        return false;
      }
      std::wstring value;
      if (!ReadNext(argc, argv, index, argument.c_str(), &value, error)) return false;
      (*settings)[key] = {std::move(value), 0};
      index += 2;
    }
    if (settings->contains(L"no-auth") && settings->contains(L"unsafe-token")) {
      *error = L"--no-auth cannot be used together with --unsafe-token.";
      return false;
    }
  }
  return true;
}

AgentCommandLineParseResult ParseAgentConfiguration(int argc, const wchar_t* const argv[],
    const std::vector<unsigned char>& document, const std::wstring& path) {
  auto options = DefaultAgentCommandLineOptions();
  options.config_path = path;
  Settings cli, ini;
  bool help = false;
  std::wstring error;
  if (!ParseCli(argc, argv, &cli, &help, &error)) return ParseFailure(options, error);
  if (help) return ParseSuccess(options, true);
  if (!ParseIni(document, path, &ini, &error)) return ParseFailure(options, error);
  const bool cli_auth = cli.contains(L"no-auth") || cli.contains(L"unsafe-token");
  for (const auto key : kSettingNames) {
    const bool auth = std::wstring(key) == L"no-auth" || std::wstring(key) == L"unsafe-token";
    const Setting* setting = nullptr;
    if (const auto found = cli.find(key); found != cli.end()) setting = &found->second;
    else if (!auth || !cli_auth) {
      if (const auto found = ini.find(key); found != ini.end()) setting = &found->second;
    }
    if (!setting) continue;
    const auto& value = setting->value;
    const auto fail = [&](const wchar_t* reason) {
      return ParseFailure(options, Diagnostic(path, setting->line, key, reason));
    };
    if (value.empty() && (std::wstring(key) != L"unsafe-token" || !setting->line))
      return fail(L"Value cannot be empty.");
    if (std::wstring(key) == L"host") {
      if (!NarrowAsciiStrict(value, &options.host)) return fail(L"Host must contain ASCII text.");
    } else if (std::wstring(key) == L"port") {
      if (!ParsePort(value, &options.port)) return fail(L"Expected an integer from 0 to 65535.");
    } else if (std::wstring(key) == L"connection-mode") {
      if (value != L"single" && value != L"multiple") return fail(L"Expected single or multiple.");
      options.multiple_connections = value == L"multiple";
    } else if (std::wstring(key) == L"max-transfer-size") {
      if (!ParseTransferSize(value, &options.max_transfer_bytes))
        return fail(L"Expected a positive integer in MiB that fits in a 64-bit byte count.");
    } else if (std::wstring(key) == L"no-auth") {
      if (value != L"true" && value != L"false") return fail(L"Expected true or false.");
      options.auth_required = value == L"false";
    } else if (std::wstring(key) == L"unsafe-token") {
      if (!NarrowAsciiStrict(value, &options.auth_token) || options.auth_token.size() > kAuthTokenMaxBytes)
        return fail(L"Expected ASCII text within the maximum token byte length.");
      if (!options.auth_required && !options.auth_token.empty())
        return fail(L"Conflicts with no-auth=true.");
    } else if (std::wstring(key) == L"safe-base-dir") {
      options.safe_base_dir = value;
      options.safe_base_dir_line = setting->line;
    }
  }
  return ParseSuccess(options, false);
}

AgentCommandLineParseResult ParseAgentCommandLine(int argc, const wchar_t* const argv[]) {
  return ParseAgentConfiguration(argc, argv, {}, L"");
}

}  // namespace agent_rover
