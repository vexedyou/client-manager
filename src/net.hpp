// net.hpp — net.conn collector.
#pragma once

#include "json.hpp"

namespace cm {

// proto, local addr:port, remote addr:port, state, owning pid.
// IP Helper GetExtendedTcpTable (TCP_TABLE_OWNER_PID / UDP_TABLE_OWNER_PID).
JsonValue CollectNetConn();

}  // namespace cm
