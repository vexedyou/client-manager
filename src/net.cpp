// net.cpp — net.conn via IP Helper GetExtendedTcpTable (owner PID).
#include "net.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tcpmib.h>

#include <algorithm>
#include <vector>

#include "util.hpp"

namespace cm {

namespace {

const wchar_t* TcpStateName(DWORD s) {
    switch (s) {
        case MIB_TCP_STATE_CLOSED: return L"CLOSED";
        case MIB_TCP_STATE_LISTEN: return L"LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return L"SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return L"CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return L"LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return L"DELETE_TCB";
        default: return L"UNKNOWN";
    }
}

std::wstring AddrV4(DWORD networkOrderAddr) {
    wchar_t buf[64] = {};
    if (InetNtopW(AF_INET, &networkOrderAddr, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0]))) == nullptr) {
        return L"";
    }
    return std::wstring(buf);
}

std::wstring AddrV6(const BYTE* addr16) {
    wchar_t buf[256] = {};
    if (InetNtopW(AF_INET6, addr16, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0]))) == nullptr) {
        return L"";
    }
    return std::wstring(buf);
}

void AddTcpTable(std::vector<JsonValue>& out, ULONG af) {
    DWORD size = 0;
    DWORD ret = GetExtendedTcpTable(nullptr, &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER && ret != ERROR_SUCCESS) return;
    std::vector<BYTE> buf(size);
    if (af == AF_INET) {
        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
        ret = GetExtendedTcpTable(table, &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
        if (ret != ERROR_SUCCESS) return;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID& r = table->table[i];
            JsonValue e = JsonValue::object();
            e.set("proto", JsonValue::stringWide(L"TCP"));
            e.set("family", JsonValue::stringWide(L"IPv4"));
            e.set("local_addr", JsonValue::stringWide(AddrV4(r.dwLocalAddr)));
            e.set("local_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwLocalPort))));
            e.set("remote_addr", JsonValue::stringWide(AddrV4(r.dwRemoteAddr)));
            e.set("remote_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwRemotePort))));
            e.set("state", JsonValue::stringWide(std::wstring(TcpStateName(r.dwState))));
            e.set("pid", JsonValue::unsignedInteger(r.dwOwningPid));
            out.push_back(std::move(e));
        }
    } else {
        auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
        ret = GetExtendedTcpTable(table, &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0);
        if (ret != ERROR_SUCCESS) return;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCP6ROW_OWNER_PID& r = table->table[i];
            JsonValue e = JsonValue::object();
            e.set("proto", JsonValue::stringWide(L"TCP"));
            e.set("family", JsonValue::stringWide(L"IPv6"));
            e.set("local_addr", JsonValue::stringWide(AddrV6(r.ucLocalAddr)));
            e.set("local_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwLocalPort))));
            e.set("remote_addr", JsonValue::stringWide(AddrV6(r.ucRemoteAddr)));
            e.set("remote_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwRemotePort))));
            e.set("state", JsonValue::stringWide(std::wstring(TcpStateName(r.dwState))));
            e.set("pid", JsonValue::unsignedInteger(r.dwOwningPid));
            out.push_back(std::move(e));
        }
    }
}

void AddUdpTable(std::vector<JsonValue>& out, ULONG af) {
    DWORD size = 0;
    DWORD ret = GetExtendedUdpTable(nullptr, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER && ret != ERROR_SUCCESS) return;
    std::vector<BYTE> buf(size);
    if (af == AF_INET) {
        auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
        ret = GetExtendedUdpTable(table, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (ret != ERROR_SUCCESS) return;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_UDPROW_OWNER_PID& r = table->table[i];
            JsonValue e = JsonValue::object();
            e.set("proto", JsonValue::stringWide(L"UDP"));
            e.set("family", JsonValue::stringWide(L"IPv4"));
            e.set("local_addr", JsonValue::stringWide(AddrV4(r.dwLocalAddr)));
            e.set("local_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwLocalPort))));
            e.set("remote_addr", JsonValue::stringWide(L""));
            e.set("remote_port", JsonValue::unsignedInteger(0));
            e.set("state", JsonValue::stringWide(L"NONE"));
            e.set("pid", JsonValue::unsignedInteger(r.dwOwningPid));
            out.push_back(std::move(e));
        }
    } else {
        auto* table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(buf.data());
        ret = GetExtendedUdpTable(table, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (ret != ERROR_SUCCESS) return;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_UDP6ROW_OWNER_PID& r = table->table[i];
            JsonValue e = JsonValue::object();
            e.set("proto", JsonValue::stringWide(L"UDP"));
            e.set("family", JsonValue::stringWide(L"IPv6"));
            e.set("local_addr", JsonValue::stringWide(AddrV6(r.ucLocalAddr)));
            e.set("local_port", JsonValue::unsignedInteger(ntohs(static_cast<u_short>(r.dwLocalPort))));
            e.set("remote_addr", JsonValue::stringWide(L""));
            e.set("remote_port", JsonValue::unsignedInteger(0));
            e.set("state", JsonValue::stringWide(L"NONE"));
            e.set("pid", JsonValue::unsignedInteger(r.dwOwningPid));
            out.push_back(std::move(e));
        }
    }
}

}  // namespace

JsonValue CollectNetConn() {
    JsonValue list = JsonValue::array();
    std::vector<JsonValue> all;
    AddTcpTable(all, AF_INET);
    AddTcpTable(all, AF_INET6);
    AddUdpTable(all, AF_INET);
    AddUdpTable(all, AF_INET6);

    std::sort(all.begin(), all.end(), [](const JsonValue& a, const JsonValue& b) {
        auto getS = [](const JsonValue& v, const char* k) -> std::string {
            const JsonValue* f = v.find(k);
            return (f && f->isString()) ? f->asString() : std::string();
        };
        auto getN = [](const JsonValue& v, const char* k) -> uint64_t {
            const JsonValue* f = v.find(k);
            return (f && f->isNumber()) ? f->asUInt() : 0;
        };
        std::string pa = getS(a, "proto"), pb = getS(b, "proto");
        if (pa != pb) return pa < pb;
        uint64_t la = getN(a, "local_port"), lb = getN(b, "local_port");
        if (la != lb) return la < lb;
        return getN(a, "pid") < getN(b, "pid");
    });

    for (auto& e : all) list.push(std::move(e));
    return list;
}

}  // namespace cm
