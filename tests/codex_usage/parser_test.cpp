#include "codex_usage.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace codex_usage;
const std::string payload =
    R"({"version":1,"source":"My Mac","primary":{"usedPercent":37.4,"windowDurationMins":300,"resetsAt":1794000000},"secondary":{"usedPercent":91,"windowDurationMins":10080,"resetsAt":1794500000}})";
int main()
{
    Snapshot result;
    assert(parsePayload(payload, result));
    assert(result.valid && !result.stale && result.source == "My Mac");
    assert(result.primary.usedPercent == 37 && result.secondary.usedPercent == 91);
    assert(windowName(result.primary, "Primary") == "5h");
    assert(windowName(result.secondary, "Secondary") == "7d");
    assert(resetText(result.primary, 1793996400) == "Resets in 1h 0m");
    assert(resetText(result.primary, 1794000000) == "Awaiting quota refresh");
    assert(resetText(result.primary, 0) == "Waiting for clock sync");
    for (const std::string& invalid :
         {"", "null", "[]", "{}", "{", "{\"version\":2}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":null,\"secondary\":null}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":101}}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":-1}}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":\"0\"}}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":0,\"resetsAt\":-1}}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":0,\"windowDurationMins\":0}}",
          "{\"version\":1,\"source\":\"pc\",\"primary\":{\"usedPercent\":0,\"windowDurationMins\":1.5}}",
          "{\"version\":1,\"source\":\"pc\\n\",\"primary\":{\"usedPercent\":0}}"}) {
        assert(!parsePayload(invalid, result));
        assert(result.source == "My Mac" && result.primary.usedPercent == 37);
    }
    assert(!parsePayload(payload + "junk", result));
    assert(!parsePayload(payload + std::string(1, '\0'), result));
    assert(!parsePayload(std::string(2049, ' '), result));
    assert(parsePayload(
        R"({"version":1,"source":"pc","primary":null,"secondary":{"usedPercent":0,"windowDurationMins":null,"resetsAt":null}})",
        result));
    assert(!result.primary.valid && result.secondary.valid && result.secondary.usedPercent == 0);
    assert(windowName(result.secondary, "Secondary") == "Secondary");
    assert(resetText(result.secondary, 1793996400) == "Reset time unavailable");
    std::cout << "PASS: quota parsing, invalid payload isolation, optional windows, reset display\n";
}
