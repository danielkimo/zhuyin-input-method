#ifdef _WIN32

#include "ClassFactory.h"

#include <new>

#include "Globals.h"
#include "TextService.h"

namespace zhuyin::windowsime {

ClassFactory::ClassFactory() : ref_count_(1) { DllAddRef(); }

ClassFactory::~ClassFactory() { DllRelease(); }

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == nullptr) {
    return E_INVALIDARG;
  }
  *ppvObject = nullptr;

  if (riid == IID_IUnknown || riid == IID_IClassFactory) {
    *ppvObject = static_cast<IClassFactory*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
  const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
  if (remaining == 0) {
    delete this;
  }
  return remaining;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID riid,
                                          void** ppvObject) {
  if (ppvObject == nullptr) {
    return E_INVALIDARG;
  }
  *ppvObject = nullptr;

  if (outer != nullptr) {
    return CLASS_E_NOAGGREGATION;
  }

  auto* service = new (std::nothrow) TextService();
  if (service == nullptr) {
    return E_OUTOFMEMORY;
  }

  const HRESULT hr = service->QueryInterface(riid, ppvObject);
  service->Release();
  return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
  if (lock) {
    DllLock();
  } else {
    DllUnlock();
  }
  return S_OK;
}

}  // namespace zhuyin::windowsime

#endif  // _WIN32
