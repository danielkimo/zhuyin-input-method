#pragma once

#ifdef _WIN32

#include <Windows.h>

namespace zhuyin::windowsime {

HRESULT RegisterServer();
HRESULT UnregisterServer();

}  // namespace zhuyin::windowsime

#endif  // _WIN32
