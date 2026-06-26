// agent-rover - A multi-platform TypeScript test driver for GUI applications
// Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/agent-rover

#include "win32_eventlog.h"

#include <windows.h>
#include <winnt.h>

#include <cstdio>
#include <cwchar>
#include <ctime>
#include <string>
#include <vector>

#include "win32_util.h"

namespace agent_rover {

static std::string EventTypeLabel(WORD type) {
  switch (type) {
    case EVENTLOG_ERROR_TYPE:
      return "Error";
    case EVENTLOG_WARNING_TYPE:
      return "Warning";
    case EVENTLOG_INFORMATION_TYPE:
      return "Information";
    case EVENTLOG_AUDIT_SUCCESS:
      return "Information";
    case EVENTLOG_AUDIT_FAILURE:
      return "Error";
    default:
      return "Information";
  }
}

static std::string IsoTimestamp(DWORD seconds) {
  const time_t raw = static_cast<time_t>(seconds);
  tm* utc = gmtime(&raw);
  if (utc == nullptr) {
    return std::string();
  }
  char buffer[32] = {};
  std::snprintf(
      buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
      utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, utc->tm_hour,
      utc->tm_min, utc->tm_sec);
  return std::string(buffer);
}

static std::string ReadWideString(const wchar_t* value) {
  return WideToUtf8(std::wstring(value));
}

static const wchar_t* NextWideString(const wchar_t* value) {
  return value + std::wcslen(value) + 1;
}

static std::string ReadInsertionStrings(const EVENTLOGRECORD* record) {
  if (record->NumStrings == 0) {
    char buffer[64] = {};
    std::snprintf(
        buffer, sizeof(buffer), "Event %lu.",
        static_cast<unsigned long>(record->EventID & 0xffff));
    return std::string(buffer);
  }

  std::string message;
  const wchar_t* current = reinterpret_cast<const wchar_t*>(
      reinterpret_cast<const unsigned char*>(record) + record->StringOffset);
  for (WORD index = 0; index < record->NumStrings; index += 1) {
    if (!message.empty()) {
      message += " ";
    }
    message += ReadWideString(current);
    current = NextWideString(current);
  }
  return message;
}

static EventLogEntry ToEntry(const EVENTLOGRECORD* record) {
  const wchar_t* source = reinterpret_cast<const wchar_t*>(
      reinterpret_cast<const unsigned char*>(record) + sizeof(EVENTLOGRECORD));
  return {
      static_cast<uint32_t>(record->EventID & 0xffff),
      ReadWideString(source),
      EventTypeLabel(record->EventType),
      IsoTimestamp(record->TimeGenerated),
      ReadInsertionStrings(record),
  };
}

static bool EntryMatches(const EventLogEntry& entry, const EventLogQuery& query) {
  if (!query.source.empty() && entry.provider != query.source) {
    return false;
  }
  if (!query.since.empty() && entry.timestamp < query.since) {
    return false;
  }
  return true;
}

bool ReadEventLogs(
    const EventLogQuery& query,
    std::vector<EventLogEntry>* entries,
    std::string* error) {
  entries->clear();
  const uint32_t max_entries = query.max_entries == 0 ? 50 : query.max_entries;
  HANDLE log = OpenEventLogW(nullptr, L"Application");
  if (log == nullptr) {
    *error = "OpenEventLogW Application failed.";
    return false;
  }

  std::vector<unsigned char> buffer(64 * 1024);
  for (;;) {
    DWORD bytes_read = 0;
    DWORD minimum_bytes = 0;
    const BOOL ok = ReadEventLogW(
        log, EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ, 0,
        buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
        &minimum_bytes);
    if (!ok) {
      const DWORD last_error = GetLastError();
      if (last_error == ERROR_HANDLE_EOF) {
        CloseEventLog(log);
        return true;
      }
      if (last_error == ERROR_INSUFFICIENT_BUFFER && minimum_bytes > 0) {
        buffer.assign(minimum_bytes, 0);
        continue;
      }
      CloseEventLog(log);
      *error = "ReadEventLogW failed.";
      return false;
    }

    DWORD offset = 0;
    while (offset < bytes_read && entries->size() < max_entries) {
      const EVENTLOGRECORD* record = reinterpret_cast<const EVENTLOGRECORD*>(
          buffer.data() + offset);
      const EventLogEntry entry = ToEntry(record);
      if (EntryMatches(entry, query)) {
        entries->push_back(entry);
      }
      offset += record->Length;
    }
    if (entries->size() >= max_entries) {
      CloseEventLog(log);
      return true;
    }
  }
}

}  // namespace agent_rover
