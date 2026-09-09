#include "teez/core/runner.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>

#include "teez/core/config.hpp"
#include "teez/core/coverage.hpp"
#include "teez/core/coverage_program.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/discovery.hpp"
#include "teez/core/exit_code.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/process.hpp"
#include "teez/core/runner_config.hpp"
#include "teez/core/signals.hpp"

namespace teez::core {

namespace {

bool is_failure_event(const nlohmann::json& event) {
    if (!event.is_object() || !event.contains("event")) {
        return false;
    }
    const auto type = event.at("event").get<std::string>();
    return type == "fail" || type == "error";
}

void emit_event(const TestEventCallback& on_event, const nlohmann::json& event) {
    on_event(event);
}

class LineCallbackStreambuf final : public std::streambuf {
  public:
    explicit LineCallbackStreambuf(std::function<void(const std::string&)> on_line)
        : on_line_(std::move(on_line)) {}

  protected:
    int overflow(int character) override {
        if (character == traits_type::eof()) {
            flush_line();
            return traits_type::not_eof(character);
        }

        const char ch = static_cast<char>(character);
        if (ch == '\n') {
            flush_line();
            return character;
        }

        buffer_.push_back(ch);
        return character;
    }

    int sync() override {
        flush_line();
        return 0;
    }

  private:
    std::string buffer_;
    std::function<void(const std::string&)> on_line_;

    void flush_line() {
        if (buffer_.empty()) {
            return;
        }
        on_line_(buffer_);
        buffer_.clear();
    }
};

void apply_command_env(const CommandSpec& spec) {
    for (const auto& [key, value] : spec.env) {
        setenv(key.c_str(), value.c_str(), 1);
    }
}

bool spec_has_worker_subcommand(const CommandSpec& spec) {
    const auto subcommand = worker_subcommand();
    if (!subcommand.has_value()) {
        return false;
    }
    return !spec.args.empty() && spec.args.front() == *subcommand;
}

bool spec_has_worker_list_flag(const CommandSpec& spec) {
    return std::find(spec.args.begin(), spec.args.end(), "--list") != spec.args.end();
}

RunContext context_for_runner(const RunContext& context, const DiscoveryMatch& match,
                              const DiscoveryContext& discovery) {
    RunContext runner_context = context;
    runner_context.runner_name = match.manifest.name;
    runner_context.runner_options = resolve_effective_runner_options(
        match.manifest.include, match.manifest.exclude, discovery.runners, match.manifest.name);
    return runner_context;
}

TestEventCallback synchronized_event_callback(const TestEventCallback& on_event,
                                              std::mutex& mutex) {
    return [&on_event, &mutex](const nlohmann::json& event) {
        std::lock_guard lock(mutex);
        on_event(event);
    };
}

int run_matches_sequential(const std::vector<DiscoveryMatch>& matches, const RunContext& context,
                           const DiscoveryContext& discovery, const TestEventCallback& on_event) {
    int exit_code = kExitSuccess;
    for (const auto& match : matches) {
        exit_code = std::max(
            exit_code, run_with_plugin(match.manifest.plugin_file,
                                       context_for_runner(context, match, discovery), on_event));
    }
    return exit_code;
}

int run_matches_parallel(const std::vector<DiscoveryMatch>& matches, const RunContext& context,
                         const DiscoveryContext& discovery, const TestEventCallback& on_event) {
    std::mutex event_mutex;
    const auto sync_on_event = synchronized_event_callback(on_event, event_mutex);
    std::vector<std::future<int>> futures;
    futures.reserve(matches.size());

    for (const auto& match : matches) {
        const auto plugin_path = match.manifest.plugin_file;
        const auto runner_context = context_for_runner(context, match, discovery);
        futures.push_back(
            std::async(std::launch::async, [plugin_path, runner_context, sync_on_event]() {
                return run_with_plugin(plugin_path, runner_context, sync_on_event);
            }));
    }

    int exit_code = kExitSuccess;
    for (auto& future : futures) {
        exit_code = std::max(exit_code, future.get());
    }
    return exit_code;
}

std::optional<std::filesystem::path> worker_target_from_spec(const CommandSpec& spec) {
    std::size_t index = 0;
    if (spec_has_worker_subcommand(spec)) {
        ++index;
    }

    for (; index < spec.args.size(); ++index) {
        const auto& arg = spec.args[index];
        if (arg == "--list" || arg == "--update-snapshots") {
            continue;
        }
        return std::filesystem::path(arg);
    }

    return std::nullopt;
}

bool is_worker_plugin_command(const CommandSpec& spec) {
    if (std::filesystem::path(spec.command) != worker_binary()) {
        return false;
    }

    if (worker_subcommand().has_value()) {
        return spec_has_worker_subcommand(spec);
    }

    return !spec.args.empty();
}

bool is_in_process_worker_command(const CommandSpec& spec) {
    if (in_process_worker() == nullptr || !is_worker_plugin_command(spec)) {
        return false;
    }
    if (spec_has_worker_list_flag(spec)) {
        return false;
    }
    return worker_target_from_spec(spec).has_value();
}

bool is_in_process_worker_list_command(const CommandSpec& spec) {
    if (in_process_worker_list() == nullptr || !is_worker_plugin_command(spec)) {
        return false;
    }
    if (!spec_has_worker_list_flag(spec)) {
        return false;
    }
    return worker_target_from_spec(spec).has_value();
}

std::vector<std::string> list_worker_via_command(const CommandSpec& spec) {
    apply_command_env(spec);
    const auto target = worker_target_from_spec(spec);
    if (!target.has_value()) {
        throw std::runtime_error("worker list command is missing target path");
    }

    if (is_in_process_worker_list_command(spec)) {
        return (*in_process_worker_list())(*target);
    }

    const auto result = run_command_capture(spec);
    if (result.exit_code != 0) {
        std::ostringstream message;
        message << "list command failed with exit code " << result.exit_code << " ("
                << spec.command;
        for (const auto& arg : spec.args) {
            message << ' ' << arg;
        }
        message << ')';
        if (!result.stderr_text.empty()) {
            message << ": " << result.stderr_text;
        }
        throw std::runtime_error(message.str());
    }

    std::vector<std::string> tests;
    std::istringstream lines(result.stdout_text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) {
            tests.push_back(line);
        }
    }
    return tests;
}

int run_in_process_worker(const CommandSpec& spec, Plugin& plugin,
                          const TestEventCallback& on_event) {
    const auto* worker_fn = in_process_worker();
    if (worker_fn == nullptr) {
        throw std::runtime_error("in-process worker is not configured");
    }

    apply_command_env(spec);
    bool saw_failure = false;

    LineCallbackStreambuf buffer([&](const std::string& line) {
        const std::string event_json = plugin.parse_line_ndjson(line);
        if (event_json.empty()) {
            return;
        }
        const auto event = parse_plugin_event_json(event_json);
        if (!event.has_value()) {
            return;
        }
        if (is_failure_event(*event)) {
            saw_failure = true;
        }
        emit_event(on_event, *event);
    });
    std::ostream worker_out(&buffer);

    const auto target = worker_target_from_spec(spec);
    if (!target.has_value()) {
        throw std::runtime_error("worker run command is missing target path");
    }

    const int child_exit_code = (*worker_fn)(*target, worker_out);
    worker_out.flush();
    return normalize_exit_code(interruption_requested(), saw_failure, child_exit_code);
}

} // namespace

std::optional<nlohmann::json> parse_plugin_event_json(const std::string& event_json) {
    const auto json = nlohmann::json::parse(event_json, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }
    return json;
}

TestEventCallback make_ndjson_event_writer(std::ostream& out) {
    return [&out](const nlohmann::json& event) {
        out << event.dump() << '\n';
        out.flush();
    };
}

int run_with_plugin(const std::filesystem::path& plugin_path, const RunContext& context,
                    const TestEventCallback& on_event) {
    Plugin plugin(plugin_path);

    for (const auto& test_id : plugin.list_tests(context)) {
        emit_event(on_event, {{"event", "start"}, {"id", test_id}});
    }

    const CommandSpec spec = plugin.build_command(context);
    bool saw_failure = false;

    if (is_in_process_worker_command(spec)) {
        return run_in_process_worker(spec, plugin, on_event);
    }

    const int child_exit_code = run_command_streaming(spec, [&](const std::string& line) {
        const std::string event_json = plugin.parse_line_ndjson(line);
        if (event_json.empty()) {
            return;
        }
        const auto event = parse_plugin_event_json(event_json);
        if (!event.has_value()) {
            return;
        }
        if (is_failure_event(*event)) {
            saw_failure = true;
        }
        emit_event(on_event, *event);
    });

    return normalize_exit_code(interruption_requested(), saw_failure, child_exit_code);
}

int run_with_plugin(const std::filesystem::path& plugin_path, const RunContext& context,
                    std::ostream& out) {
    return run_with_plugin(plugin_path, context, make_ndjson_event_writer(out));
}

int run_context(const DiscoveryContext& discovery, const RunContext& context,
                const TestEventCallback& on_event) {
    const auto matches = find_all_runner_matches(context.target_path, discovery);
    if (matches.empty()) {
        throw std::runtime_error("no matching plugin found for: " + context.target_path.string());
    }

    if (discovery.parallel_runners && matches.size() > 1) {
        return run_matches_parallel(matches, context, discovery, on_event);
    }
    return run_matches_sequential(matches, context, discovery, on_event);
}

int run_context(const DiscoveryContext& discovery, const RunContext& context, std::ostream& out) {
    return run_context(discovery, context, make_ndjson_event_writer(out));
}

int run_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                const TestEventCallback& on_event) {
    return run_context(DiscoveryContext::bundled_only(plugins_dir), context, on_event);
}

int run_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                std::ostream& out) {
    return run_context(DiscoveryContext::bundled_only(plugins_dir), context, out);
}

std::vector<std::string> list_context(const std::filesystem::path& plugins_dir,
                                      const RunContext& context) {
    return list_context(DiscoveryContext::bundled_only(plugins_dir), context);
}

int run_coverage_context(const DiscoveryContext& discovery, const RunContext& context,
                         const TestEventCallback& on_event) {
    const auto plugin_path = discover_plugin(context.target_path, discovery);
    if (!plugin_path.has_value()) {
        throw std::runtime_error("no matching plugin found for: " + context.target_path.string());
    }

    Plugin plugin(*plugin_path);
    if (!plugin.supports_coverage_run()) {
        throw std::runtime_error("plugin does not implement build_coverage_run: " +
                                 plugin_path->string());
    }

    const auto spec = plugin.build_coverage_run(context);
    if (!spec.has_value()) {
        throw std::runtime_error("plugin does not implement build_coverage_run");
    }

    bool saw_failure = false;
    const auto on_line = [&](const std::string& line) {
        const std::string event_json = plugin.parse_line_ndjson(line);
        if (!event_json.empty()) {
            const auto event = parse_plugin_event_json(event_json);
            if (event.has_value()) {
                if (is_failure_event(*event)) {
                    saw_failure = true;
                }
                emit_event(on_event, *event);
                return;
            }
        }
        if (!line.empty()) {
            emit_event(on_event, {{"event", "output"}, {"text", line}});
        }
    };

    int child_exit_code = 0;
    if (spec->working_directory.has_value()) {
        const auto previous = std::filesystem::current_path();
        std::filesystem::current_path(*spec->working_directory);
        try {
            child_exit_code = run_command_streaming(spec->command, on_line);
            std::filesystem::current_path(previous);
        } catch (...) {
            std::filesystem::current_path(previous);
            throw;
        }
    } else {
        child_exit_code = run_command_streaming(spec->command, on_line);
    }

    if (spec->collect_command.has_value()) {
        ExecResult collect_result;
        if (spec->working_directory.has_value()) {
            const auto previous = std::filesystem::current_path();
            std::filesystem::current_path(*spec->working_directory);
            try {
                collect_result = run_command_capture(*spec->collect_command);
                std::filesystem::current_path(previous);
            } catch (...) {
                std::filesystem::current_path(previous);
                throw;
            }
        } else {
            collect_result = run_command_capture(*spec->collect_command);
        }
        if (collect_result.exit_code != 0) {
            throw std::runtime_error("coverage collect command failed with exit code " +
                                     std::to_string(collect_result.exit_code));
        }
    }

    if (!spec->report_path.has_value()) {
        throw std::runtime_error("coverage program requires report or report_path");
    }

    const auto report_file = spec->working_directory.has_value()
                                 ? *spec->working_directory / *spec->report_path
                                 : *spec->report_path;
    if (!std::filesystem::exists(report_file)) {
        throw std::runtime_error("coverage report not found: " + report_file.string());
    }

    const auto table = load_coverage_table(report_file);
    emit_event(on_event, coverage_table_to_event(table));

    CoverageThresholdResult threshold;
    threshold.passed = true;
    if (spec->min_line_rate.has_value()) {
        CoverageThresholds thresholds;
        thresholds.min_line_rate = spec->min_line_rate;
        threshold = check_coverage_thresholds(table, thresholds);
    }

    if (spec->reporter.has_value()) {
        auto output_path = spec->output_path;
        if (!output_path.has_value()) {
            output_path = std::filesystem::path(
                "coverage" + coverage_reporter_by_name(*spec->reporter).default_extension());
        }
        output_path = spec->working_directory.has_value() ? *spec->working_directory / *output_path
                                                          : *output_path;
        if (const auto parent = output_path->parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        write_coverage_report(table, *spec->reporter, *output_path);
    }

    if (!threshold.passed) {
        return kExitFailure;
    }
    return normalize_exit_code(interruption_requested(), saw_failure, child_exit_code);
}

int run_coverage_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                         const TestEventCallback& on_event) {
    return run_coverage_context(DiscoveryContext::bundled_only(plugins_dir), context, on_event);
}

int run_coverage_context(const std::filesystem::path& plugins_dir, const RunContext& context) {
    return run_coverage_context(plugins_dir, context, [](const nlohmann::json&) {});
}

int run_coverage_context(const DiscoveryContext& discovery, const RunContext& context) {
    return run_coverage_context(discovery, context, [](const nlohmann::json&) {});
}

std::vector<std::string> list_context(const DiscoveryContext& discovery,
                                      const RunContext& context) {
    const auto matches = find_all_runner_matches(context.target_path, discovery);
    if (matches.empty()) {
        throw std::runtime_error("no matching plugin found for: " + context.target_path.string());
    }

    const auto collect_tests = [&](const DiscoveryMatch& match) {
        const auto runner_context = context_for_runner(context, match, discovery);
        Plugin plugin(match.manifest.plugin_file);
        if (const auto spec = plugin.build_list_command(runner_context); spec.has_value()) {
            return list_worker_via_command(*spec);
        }
        return plugin.list_tests(runner_context);
    };

    std::vector<std::string> tests;
    if (discovery.parallel_runners && matches.size() > 1) {
        std::mutex tests_mutex;
        std::vector<std::future<std::vector<std::string>>> futures;
        futures.reserve(matches.size());
        for (const auto& match : matches) {
            futures.push_back(std::async(
                std::launch::async, [&collect_tests, match]() { return collect_tests(match); }));
        }
        for (auto& future : futures) {
            const auto runner_tests = future.get();
            std::lock_guard lock(tests_mutex);
            tests.insert(tests.end(), runner_tests.begin(), runner_tests.end());
        }
        return tests;
    }

    for (const auto& match : matches) {
        const auto runner_tests = collect_tests(match);
        tests.insert(tests.end(), runner_tests.begin(), runner_tests.end());
    }
    return tests;
}

} // namespace teez::core
