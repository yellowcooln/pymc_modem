#include "effective_tcp_port.h"
#include "webui_shared.h"
#include <cassert>
#include <string>

int main() {
#if defined(BOARD_ETHERMESH_DUO)
    assert(effectiveTcpPort(5056) == 5055);
    assert(effectiveTcpPort(0) == 5055);
    assert(!acceptsTcpPort(5056));
    assert(acceptsTcpPort(5055));
#else
    assert(effectiveTcpPort(5056) == 5056);
    assert(acceptsTcpPort(5056));
#endif
    WebUiShared::Model model;
    model.capabilities.writableManagement = true;
    model.config.tcpPort = effectiveTcpPort(5056);
#if defined(BOARD_ETHERMESH_DUO)
    model.capabilities.fixedTcpPort = true;
    const auto html = WebUiShared::renderRootPage(model);
    assert(html.find("Fixed diagnostic TCP port: 5055") != std::string::npos);
    assert(html.find("name='port'") == std::string::npos);
    const auto stats = WebUiShared::renderStatsPage(model);
    assert(stats.find("5055") != std::string::npos);
    assert(stats.find("<h3>Radio</h3>") == std::string::npos);
    assert(WebUiShared::renderNetworkJson(model).find("\"tcp_port\":5055") != std::string::npos);
    assert(WebUiShared::renderConfigJson(model).find("\"tcp_port\":5055") != std::string::npos);
#else
    const auto html = WebUiShared::renderRootPage(model);
    assert(html.find("name='port'") != std::string::npos);
    assert(html.find("value='5056'") != std::string::npos);
    assert(WebUiShared::renderNetworkJson(model).find("\"tcp_port\":5056") != std::string::npos);
#endif
}
