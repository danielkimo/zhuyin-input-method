#ifdef _WIN32

#include <Windows.h>

#include <new>

#include "ClassFactory.h"
#include "Globals.h"
#include "Registrar.h"

namespace zhuyin::windowsime {

HINSTANCE g_hInstance = nullptr;
namespace {
volatile LONG g_object_count = 0;
volatile LONG g_lock_count = 0;
}  // namespace

void DllAddRef() { InterlockedIncrement(&g_object_count); }
void DllRelease() { InterlockedDecrement(&g_object_count); }
void DllLock() { InterlockedIncrement(&g_lock_count); }
void DllUnlock() { InterlockedDecrement(&g_lock_count); }
long DllObjectCount() { return g_object_count; }
long DllLockCount() { return g_lock_count; }

}  // namespace zhuyin::windowsime

using zhuyin::windowsime::ClassFactory;
using zhuyin::windowsime::DllLockCount;
using zhuyin::windowsime::DllObjectCount;
using zhuyin::windowsime::RegisterServer;
using zhuyin::windowsime::UnregisterServer;
using zhuyin::windowsime::g_hInstance;
using zhuyin::windowsime::kTextServiceClsid;

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInstance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllCanUnloadNow(void) {
  return (DllObjectCount() == 0 && DllLockCount() == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }
  *ppv = nullptr;

  if (!IsEqualCLSID(clsid, kTextServiceClsid)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  auto* factory = new (std::nothrow) ClassFactory();
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }

  const HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

STDAPI DllRegisterServer(void) { return RegisterServer(); }

STDAPI DllUnregisterServer(void) { return UnregisterServer(); }

#endif  // _WIN32
