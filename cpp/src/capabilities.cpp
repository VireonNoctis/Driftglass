#include "driftglass/core.hpp"

namespace driftglass {

Json capabilities_report() {
    Json linux_scope{
        {"process", "Sampled process stat metadata for the selected PID and observed descendants; short-lived processes may be missed."},
        {"network", "Sampled TCP/TCP6/UDP/UDP6 socket metadata associated with the observed process scope; ownership and network-namespace visibility can be incomplete. UDP socket state does not reveal individual datagrams or DNS contents."}
    };
    Json collector{
        {"name", "driftglass-agent"},
        {"implementation_status", "experimental"},
        {"platform", "Linux"},
        {"mechanism", "procfs polling"},
        {"implemented_categories", {"process", "network"}},
        {"category_scope", linux_scope},
        {"unavailable_categories", {"file", "system", "persistence"}},
        {"not_collected", {
            "network packet payloads",
            "DNS queries",
            "UDP datagram events and payloads",
            "file contents",
            "file access and modification events",
            "system configuration and persistence changes",
            "command-line arguments"
        }},
        {"known_limitations", {
            "Polling can miss activity that starts and stops between samples.",
            "Observation timestamps identify sampling time, not exact action time.",
            "TCP/UDP socket metadata is limited to the sampled network namespace and observable socket ownership; UDP datagrams and DNS contents are not observed.",
            "Permissions, procfs visibility, and process races can leave coverage gaps.",
            "A completed capture is not proof of comprehensive observation."
        }}
    };
    Json platforms = Json::array({
        Json{
            {"platform", "Linux"},
            {"collector_status", "experimental"},
            {"validation_status", "synthetic parser tests, C++ coordinator tests, and Rust paired-capture tests on Ubuntu 22.04/24.04 CI images; broader real-world coverage unverified"}
        },
        Json{
            {"platform", "Windows"},
            {"collector_status", "not_implemented"},
            {"validation_status", "no native observer or platform coverage tests"}
        },
        Json{
            {"platform", "macOS"},
            {"collector_status", "experimental"},
            {"validation_status", "Swift/libproc process-tree sampler builds and tests on macOS 15 CI; C++ capture can invoke it by explicit observer path; process-only snapshots, no Endpoint Security, network, file, system or persistence event source"}
        },
        Json{
            {"platform", "Other"},
            {"collector_status", "not_implemented"},
            {"validation_status", "no native observer or platform coverage tests"}
        }
    });
    return Json{
        {"schema_version", 1},
        {"collector", collector},
        {"platforms", platforms}
    };
}

} // namespace driftglass
