#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <Unknwn.h>

namespace zhuyin::windowsime {

class ClassFactory final : public IClassFactory {
 public:
  ClassFactory();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
                              void** ppvObject) override;
  STDMETHODIMP LockServer(BOOL lock) override;

 private:
  ~ClassFactory() override;

  LONG ref_count_;
};

}  // namespace zhuyin::windowsime

#endif  // _WIN32
