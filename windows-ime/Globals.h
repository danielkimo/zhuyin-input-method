#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <msctf.h>

namespace zhuyin::windowsime {

inline constexpr CLSID kTextServiceClsid =
    {0x631b07ad, 0xf09d, 0x4b89, {0xa1, 0x45, 0x22, 0x5b, 0x22, 0xa5, 0xd4, 0x81}};
inline constexpr GUID kLanguageProfileGuid =
    {0x3bd4ef0e, 0x66f3, 0x4e7a, {0x98, 0xcb, 0x7a, 0x49, 0xf2, 0x0f, 0xa5, 0x86}};
inline constexpr LANGID kTraditionalChineseTaiwanLangId = 0x0404;
inline constexpr wchar_t kTextServiceDisplayName[] = L"Zhuyin AI 注音輸入法";
inline constexpr wchar_t kTextServiceDescription[] = L"Zhuyin AI 注音輸入法";

extern HINSTANCE g_hInstance;

void DllAddRef();
void DllRelease();
void DllLock();
void DllUnlock();
long DllObjectCount();
long DllLockCount();

}  // namespace zhuyin::windowsime

#endif  // _WIN32
