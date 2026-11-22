// wmi.cpp
#include "wmi.hpp"

#include <objbase.h>

namespace cm {

using Microsoft::WRL::ComPtr;

bool WmiClient::Connect() {
    if (connected_) return true;
    ComPtr<IWbemLocator> locator;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(locator.GetAddressOf()));
    if (FAILED(hr)) { lastError_ = hr; return false; }

    ComPtr<IWbemServices> svc;
    BSTR nameSpace = SysAllocString(L"ROOT\\CIMV2");
    if (!nameSpace) { lastError_ = E_OUTOFMEMORY; return false; }
    hr = locator->ConnectServer(nameSpace, nullptr, nullptr, nullptr, 0, nullptr,
                                nullptr, &svc);
    SysFreeString(nameSpace);
    if (FAILED(hr)) { lastError_ = hr; return false; }

    hr = CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr)) { lastError_ = hr; return false; }

    svc_ = svc;
    connected_ = true;
    lastError_ = S_OK;
    return true;
}

std::wstring WmiClient::ReadProp(IWbemClassObject* obj, const wchar_t* prop) {
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = obj->Get(prop, 0, &v, nullptr, nullptr);
    if (FAILED(hr)) return L"";
    std::wstring out;
    switch (v.vt) {
        case VT_BSTR:
            if (v.bstrVal) out.assign(v.bstrVal);
            break;
        case VT_I2: out = std::to_wstring(v.iVal); break;
        case VT_UI2: out = std::to_wstring(v.uiVal); break;
        case VT_I4: out = std::to_wstring(v.lVal); break;
        case VT_UI4: out = std::to_wstring(v.ulVal); break;
        case VT_I8: out = std::to_wstring(v.llVal); break;
        case VT_UI8: out = std::to_wstring(v.ullVal); break;
        case VT_R8: out = std::to_wstring(v.dblVal); break;
        case VT_BOOL: out = v.boolVal ? L"true" : L"false"; break;
        default: break;  // VT_NULL / VT_EMPTY / arrays -> ""
    }
    VariantClear(&v);
    return out;
}

std::vector<std::map<std::wstring, std::wstring>> WmiClient::Query(
    const std::wstring& wql, const std::vector<std::wstring>& props) {
    std::vector<std::map<std::wstring, std::wstring>> rows;
    if (!Connect()) return rows;

    ComPtr<IEnumWbemClassObject> enumerator;
    BSTR lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(wql.c_str());
    HRESULT hr = svc_->ExecQuery(lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                 nullptr, &enumerator);
    if (lang) SysFreeString(lang);
    if (query) SysFreeString(query);
    if (FAILED(hr) || !enumerator) { lastError_ = hr; return rows; }

    while (true) {
        IWbemClassObject* raw = nullptr;
        ULONG returned = 0;
        hr = enumerator->Next(WBEM_INFINITE, 1, &raw, &returned);
        if (hr != S_OK || returned == 0) break;
        ComPtr<IWbemClassObject> obj;
        obj.Attach(raw);
        std::map<std::wstring, std::wstring> row;
        for (const std::wstring& p : props) {
            row[p] = ReadProp(obj.Get(), p.c_str());
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace cm
