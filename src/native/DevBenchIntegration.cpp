#include "DevBenchIntegration.h"

#include "DevBenchInspection.h"

#include <BMK/Skyrim/DevBench.h>
namespace {
constexpr BMK::Skyrim::DevBench::Inspection kInspection {
    .name = "my-preciouses",
    .descriptor
    = R"({"description":"MyPreciouses inventory classification, projected finger occupancy, assignments, effects, attached geometry and selector state. Target indices 0-4 are left thumb through pinky, 5-9 are right thumb through pinky.","readOnly":true})",
    .snapshot = DevBenchInspection::Snapshot,
    .timeoutResponse = R"({"ok":false,"error":"MyPreciouses inspection timed out waiting for the game thread"})",
    .failureResponse = R"({"ok":false,"error":"MyPreciouses inspection failed. See the plugin log."})",
};
}

void DevBenchIntegration::Register() {
    BMK::Skyrim::DevBench::RegisterInspection<kInspection>();
}
