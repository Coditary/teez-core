#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fuzz_helpers.hpp"
#include "teez/core/coverage.hpp"
#include "teez/core/coverage_msgpack.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/discovery.hpp"
#include "teez/core/exit_code.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/teez_config.hpp"
#include "teez/core/test_filter.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

constexpr const char* kPluginDir = TEEZ_PLUGIN_DIR;

const std::filesystem::path kSampleLcov =
    std::filesystem::path(kPluginDir).parent_path() / "tests/fixtures/coverage/sample.lcov";

std::filesystem::path plugin_path(const char* filename) {
    return std::filesystem::path(kPluginDir) / filename;
}

teez::core::RunContext make_run_context(const std::filesystem::path& target) {
    return teez::core::RunContext{
        .command = "run",
        .target_path = target,
    };
}

void fuzz_filter_json(const std::string& payload) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded()) {
        return;
    }

    const teez::core::TestFilters filters = teez::core::filters_from_json(json);
    const teez::core::TestDescriptor descriptor{
        .file = "tests/demo/smoke.teez.lua",
        .type = "system",
        .suites = {"Suite"},
        .name = "example test",
    };
    teez::core::matches_filter(descriptor, filters);
    teez::core::serialize_id(descriptor);
}

void fuzz_parse_line(const std::filesystem::path& plugin, const std::string& payload) {
    teez::core::Plugin lua_plugin(plugin);
    const std::string event_json = lua_plugin.parse_line_ndjson(payload);
    if (!event_json.empty()) {
        teez::core::parse_plugin_event_json(event_json);
    }
}

void fuzz_manifest_json(const std::string& payload) {
    const auto manifest_dir = teez::fuzz::make_temp_dir("teez-fuzz-manifests", payload);
    std::ofstream(manifest_dir / "fuzz-manifest.json") << payload;
    teez::core::load_manifests(manifest_dir);
    teez::core::load_harness_manifests(manifest_dir);
}

void fuzz_coverage_lcov(const std::string& payload) {
    const auto path = teez::fuzz::write_temp_file("teez-fuzz", ".lcov", payload);
    teez::core::load_coverage_table_from_lcov(path);
}

void fuzz_coverage_cobertura(const std::string& payload) {
    const auto path = teez::fuzz::write_temp_file("teez-fuzz", ".xml", payload);
    teez::core::load_coverage_table_from_cobertura(path);
}

void fuzz_coverage_msgpack(const std::string& payload) {
    const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
    teez::core::coverage_table_from_msgpack(bytes);
}

void fuzz_config_lua(const std::string& payload) {
    const auto project_dir = teez::fuzz::make_temp_dir("teez-fuzz-config", payload);
    std::ofstream(project_dir / "teez.config.lua") << payload;
    teez::core::TeezConfig::load(project_dir);
}

void fuzz_filter_glob_regex(const std::string& payload) {
    const teez::core::TestDescriptor descriptor{
        .file = "benchmark.teez.lua",
        .type = "experiment",
        .suites = {"Hyperfine demo"},
        .name = "sleep 0.01 is faster than sleep 0.02",
    };
    teez::core::string_glob_match(descriptor.name, payload);
    teez::core::regex_match(descriptor.file, payload);
}

void fuzz_discover_plugin(const std::string& payload) {
    const auto project = teez::fuzz::make_temp_dir("teez-fuzz-disc", payload);
    if (!payload.empty()) {
        switch (payload[0] % 4) {
        case 0:
            std::ofstream(project / "pytest.ini") << payload;
            break;
        case 1:
            std::ofstream(project / "CTestTestfile.cmake") << payload;
            break;
        case 2:
            std::ofstream(project / "fuzz.teez.lua") << payload;
            break;
        default:
            std::ofstream(project / "CMakeLists.txt") << payload;
            break;
        }
    }

    teez::core::discover_plugin(project, kPluginDir);
    teez::core::glob_match(project, payload.empty() ? "*.teez.lua" : payload);
}

void fuzz_plugin_build_command(const std::string& payload) {
    const auto context = make_run_context(std::filesystem::current_path());
    teez::core::Plugin dummy(plugin_path("dummy.lua"));
    dummy.build_command(context);
    teez::core::Plugin worker(plugin_path("teez-plugin-worker.lua"));
    worker.build_command(context);
}

void fuzz_exit_code_normalize(const std::string& payload) {
    const bool interrupted = !payload.empty() && (payload[0] & 1);
    const bool saw_failure = payload.size() > 1 && (payload[1] & 1);
    const int child_exit = payload.size() > 2 ? static_cast<int>(payload[2]) : 0;
    teez::core::normalize_exit_code(interrupted, saw_failure, child_exit);
}

void fuzz_ndjson_event_writer(const std::string& payload) {
    std::ostringstream out;
    const auto on_event = teez::core::make_ndjson_event_writer(out);
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        on_event(json);
        return;
    }

    for (const auto& line : teez::fuzz::split_lines(payload)) {
        const auto event = teez::core::parse_plugin_event_json(line);
        if (event.has_value()) {
            on_event(*event);
        }
    }
}

void fuzz_coverage_diff(const std::string& payload) {
    if (!std::filesystem::exists(kSampleLcov)) {
        return;
    }

    auto before = teez::core::load_coverage_table_from_lcov(kSampleLcov);
    std::string mutated = teez::core::coverage_table_to_json_string(before);
    mutated += payload;
    const auto path = teez::fuzz::write_temp_file("teez-fuzz-diff", ".lcov", mutated);
    const auto after = teez::core::load_coverage_table_from_lcov(path);
    teez::core::diff_coverage_tables(before, after);
}

enum class FuzzOp : std::uint8_t {
    FilterJson = 0,
    ParseLineDummy = 1,
    ParseLineCtest = 2,
    ParseLinePytest = 3,
    ParseLineWorker = 4,
    PluginEventJson = 5,
    ManifestJson = 6,
    CoverageLcov = 7,
    CoverageCobertura = 8,
    CoverageMsgpack = 9,
    ConfigLua = 10,
    FilterGlobRegex = 11,
    DiscoverPlugin = 12,
    PluginBuildCommand = 13,
    ExitCodeNormalize = 14,
    NdjsonEventWriter = 15,
    CoverageDiff = 16,
    Count,
};

void dispatch(FuzzOp op, const std::string& payload) {
    switch (op) {
    case FuzzOp::FilterJson:
        fuzz_filter_json(payload);
        break;
    case FuzzOp::ParseLineDummy:
        fuzz_parse_line(plugin_path("dummy.lua"), payload);
        break;
    case FuzzOp::ParseLineCtest:
        fuzz_parse_line(plugin_path("teez-plugin-ctest.lua"), payload);
        break;
    case FuzzOp::ParseLinePytest:
        fuzz_parse_line(plugin_path("teez-plugin-pytest.lua"), payload);
        break;
    case FuzzOp::ParseLineWorker:
        fuzz_parse_line(plugin_path("teez-plugin-worker.lua"), payload);
        break;
    case FuzzOp::PluginEventJson:
        teez::core::parse_plugin_event_json(payload);
        break;
    case FuzzOp::ManifestJson:
        fuzz_manifest_json(payload);
        break;
    case FuzzOp::CoverageLcov:
        fuzz_coverage_lcov(payload);
        break;
    case FuzzOp::CoverageCobertura:
        fuzz_coverage_cobertura(payload);
        break;
    case FuzzOp::CoverageMsgpack:
        fuzz_coverage_msgpack(payload);
        break;
    case FuzzOp::ConfigLua:
        fuzz_config_lua(payload);
        break;
    case FuzzOp::FilterGlobRegex:
        fuzz_filter_glob_regex(payload);
        break;
    case FuzzOp::DiscoverPlugin:
        fuzz_discover_plugin(payload);
        break;
    case FuzzOp::PluginBuildCommand:
        fuzz_plugin_build_command(payload);
        break;
    case FuzzOp::ExitCodeNormalize:
        fuzz_exit_code_normalize(payload);
        break;
    case FuzzOp::NdjsonEventWriter:
        fuzz_ndjson_event_writer(payload);
        break;
    case FuzzOp::CoverageDiff:
        fuzz_coverage_diff(payload);
        break;
    case FuzzOp::Count:
        break;
    }
}

FuzzOp decode_op(std::uint8_t byte) {
    return static_cast<FuzzOp>(byte % static_cast<std::uint8_t>(FuzzOp::Count));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 2) {
        return 0;
    }

    const auto op = decode_op(data[0]);
    const std::string payload = teez::fuzz::payload_as_string(data + 1, size - 1);
    teez::fuzz::invoke_safely([&]() { dispatch(op, payload); });
    return 0;
}
