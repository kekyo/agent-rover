// Link-only fault injection: production binaries do not include this file.
#include <windows.h>
#include <cwchar>

extern "C" {
extern BOOL (WINAPI* __real___imp_TerminateProcess)(HANDLE, UINT);
}

static BOOL WINAPI DeniedWorkerTermination(HANDLE process, UINT code) {
  // Deny forced worker termination in the host only. The independent monitor
  // still has normal OS access and must retire the unhealthy host itself.
  if (code == ERROR_TIMEOUT && std::wcsstr(GetCommandLineW(), L"--agent-server")) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return __real___imp_TerminateProcess(process, code);
}

extern "C" {
BOOL (WINAPI* __wrap___imp_TerminateProcess)(HANDLE, UINT) = DeniedWorkerTermination;
}
