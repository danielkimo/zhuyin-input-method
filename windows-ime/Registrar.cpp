#ifdef _WIN32

#include "Registrar.h"

#include <Windows.h>
#include <msctf.h>
#include <strsafe.h>

#include <string>

#include "Globals.h"

namespace zhuyin::windowsime {
namespace {

std::wstring GuidToString(REFGUID guid) {
  wchar_t buffer[64] = {};
  StringFromGUID2(guid, buffer, static_cast<int>(_countof(buffer)));
  return buffer;
}

std::wstring GetModulePath() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(g_hInstance, path, static_cast<DWORD>(_countof(path)));
  return path;
}

HRESULT WriteStringValue(HKEY root, const std::wstring& subkey,
                         const wchar_t* value_name,
                         const std::wstring& value) {
  HKEY key = nullptr;
  LONG result = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                &key, nullptr);
  if (result != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(result);
  }

  result = RegSetValueExW(
      key, value_name, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(result);
}

void DeleteTreeIfPresent(HKEY root, const std::wstring& subkey) {
  RegDeleteTreeW(root, subkey.c_str());
}

HRESULT RegisterComServer() {
  const std::wstring clsid = GuidToString(kTextServiceClsid);
  const std::wstring clsid_key = L"CLSID\\" + clsid;
  const std::wstring inproc_key = clsid_key + L"\\InprocServer32";
  const std::wstring module_path = GetModulePath();

  HRESULT hr = WriteStringValue(HKEY_CLASSES_ROOT, clsid_key, nullptr,
                                kTextServiceDescription);
  if (FAILED(hr)) {
    return hr;
  }

  hr = WriteStringValue(HKEY_CLASSES_ROOT, inproc_key, nullptr, module_path);
  if (FAILED(hr)) {
    return hr;
  }

  return WriteStringValue(HKEY_CLASSES_ROOT, inproc_key, L"ThreadingModel",
                          L"Apartment");
}

HRESULT RegisterProfiles() {
  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&profiles));
  if (FAILED(hr)) {
    return hr;
  }

  hr = profiles->Register(kTextServiceClsid);
  if (SUCCEEDED(hr) || hr == TF_E_ALREADY_EXISTS) {
    hr = S_OK;
  }
  if (FAILED(hr)) {
    profiles->Release();
    return hr;
  }

  const std::wstring module_path = GetModulePath();
  ITfInputProcessorProfileMgr* profile_mgr = nullptr;
  if (SUCCEEDED(profiles->QueryInterface(IID_ITfInputProcessorProfileMgr,
                                         reinterpret_cast<void**>(&profile_mgr)))) {
    hr = profile_mgr->RegisterProfile(
        kTextServiceClsid, kTraditionalChineseTaiwanLangId,
        kLanguageProfileGuid, kTextServiceDisplayName,
        static_cast<ULONG>(wcslen(kTextServiceDisplayName)), module_path.c_str(),
        static_cast<ULONG>(module_path.size()), 0, static_cast<HKL>(nullptr), 0,
        TRUE, 0);
    profile_mgr->Release();
  } else {
    hr = profiles->AddLanguageProfile(
        kTextServiceClsid, kTraditionalChineseTaiwanLangId,
        kLanguageProfileGuid, kTextServiceDisplayName,
        static_cast<ULONG>(wcslen(kTextServiceDisplayName)), module_path.c_str(),
        static_cast<ULONG>(module_path.size()), 0);
  }

  if (hr == TF_E_ALREADY_EXISTS) {
    hr = S_OK;
  }

  if (SUCCEEDED(hr)) {
    profiles->EnableLanguageProfile(kTextServiceClsid,
                                    kTraditionalChineseTaiwanLangId,
                                    kLanguageProfileGuid, TRUE);
  }

  profiles->Release();
  return hr;
}

HRESULT UnregisterProfiles() {
  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&profiles));
  if (SUCCEEDED(hr)) {
    ITfInputProcessorProfileMgr* profile_mgr = nullptr;
    if (SUCCEEDED(profiles->QueryInterface(IID_ITfInputProcessorProfileMgr,
                                           reinterpret_cast<void**>(&profile_mgr)))) {
      profile_mgr->UnregisterProfile(kTextServiceClsid,
                                     kTraditionalChineseTaiwanLangId,
                                     kLanguageProfileGuid, 0);
      profile_mgr->Release();
    }

    profiles->RemoveLanguageProfile(kTextServiceClsid,
                                    kTraditionalChineseTaiwanLangId,
                                    kLanguageProfileGuid);
    profiles->Unregister(kTextServiceClsid);
    profiles->Release();
  }
  return S_OK;
}

HRESULT RegisterCategories() {
  ITfCategoryMgr* category_mgr = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                reinterpret_cast<void**>(&category_mgr));
  if (FAILED(hr)) {
    return hr;
  }

  hr = category_mgr->RegisterCategory(kTextServiceClsid, GUID_TFCAT_TIP_KEYBOARD,
                                      kTextServiceClsid);
  if (SUCCEEDED(hr)) {
    hr = category_mgr->RegisterCategory(kTextServiceClsid,
                                        GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                                        kTextServiceClsid);
  }

  category_mgr->Release();
  return hr;
}

HRESULT UnregisterCategories() {
  ITfCategoryMgr* category_mgr = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                reinterpret_cast<void**>(&category_mgr));
  if (FAILED(hr)) {
    return S_OK;
  }

  category_mgr->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_TIP_KEYBOARD,
                                   kTextServiceClsid);
  category_mgr->UnregisterCategory(kTextServiceClsid,
                                   GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                                   kTextServiceClsid);
  category_mgr->Release();
  return S_OK;
}

HRESULT RunWithComApartment(HRESULT (*operation)()) {
  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool should_uninitialize = SUCCEEDED(init_hr);

  HRESULT hr = operation();

  if (should_uninitialize) {
    CoUninitialize();
  }
  return hr;
}

HRESULT DoRegisterServer() {
  HRESULT hr = RegisterComServer();
  if (FAILED(hr)) {
    return hr;
  }

  hr = RegisterProfiles();
  if (FAILED(hr)) {
    return hr;
  }

  return RegisterCategories();
}

HRESULT DoUnregisterServer() {
  UnregisterCategories();
  UnregisterProfiles();
  DeleteTreeIfPresent(HKEY_CLASSES_ROOT,
                      L"CLSID\\" + GuidToString(kTextServiceClsid));
  return S_OK;
}

}  // namespace

HRESULT RegisterServer() { return RunWithComApartment(&DoRegisterServer); }

HRESULT UnregisterServer() { return RunWithComApartment(&DoUnregisterServer); }

}  // namespace zhuyin::windowsime

#endif  // _WIN32
