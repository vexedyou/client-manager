// wmi.hpp — thin WMI (ROOT\CIMV2) query helper over COM.
// All values are returned as strings ("" for NULL/absent).
#pragma once

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <vector>

namespace cm {

class WmiClient {
public:
    WmiClient() = default;
    WmiClient(const WmiClient&) = delete;
    WmiClient& operator=(const WmiClient&) = delete;

    // Connect to ROOT\CIMV2. Returns false on failure (see GetLastError).
    bool Connect();
    bool connected() const { return connected_; }
    HRESULT lastError() const { return lastError_; }

    // Run a WQL query. `props` lists the properties to read from each row.
    // Returns one map per row (empty vector on failure).
    std::vector<std::map<std::wstring, std::wstring>> Query(
        const std::wstring& wql, const std::vector<std::wstring>& props);

private:
    Microsoft::WRL::ComPtr<IWbemServices> svc_;
    bool connected_ = false;
    HRESULT lastError_ = S_OK;

    static std::wstring ReadProp(IWbemClassObject* obj, const wchar_t* prop);
};

}  // namespace cm
