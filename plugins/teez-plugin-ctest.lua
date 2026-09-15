-- teez ctest adapter
--
-- Sensible defaults: point teez at the project root and run `teez run .`.
-- CMake configure/build run automatically when build/ has no CTest tree yet.
-- Coverage (`teez run . --with-coverage`) uses gcovr + build/coverage/ by default.

local function trim(value)
    return (tostring(value or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function truthy_env(name)
    local value = os.getenv(name)
    if value == nil then
        return false
    end
    value = trim(value):lower()
    return value ~= "" and value ~= "0" and value ~= "false" and value ~= "no"
end

local function is_ci()
    return truthy_env("CI") or truthy_env("GITHUB_ACTIONS") or truthy_env("GITLAB_CI")
end

local function runner_cfg(context)
    if type(context.runner) == "table" then
        return context.runner
    end
    return {}
end

local function context_filter(context, key)
    if type(context.ctest) ~= "table" then
        return nil
    end
    local value = context.ctest[key]
    if value == nil or value == "" then
        return nil
    end
    return value
end

local function first_string(value)
    if type(value) == "string" then
        return value
    end
    if type(value) == "table" and type(value[1]) == "string" then
        return value[1]
    end
    return nil
end

local function effective_filter(context, key)
    local from_context = context_filter(context, key)
    if from_context then
        return from_context
    end
    return first_string(runner_cfg(context)[key])
end

local function cfg_value(context, key)
    local from_context = context_filter(context, key)
    if from_context then
        return from_context
    end
    return runner_cfg(context)[key]
end

local function shallow_merge(base, overrides)
    local merged = {}
    for key, value in pairs(base) do
        merged[key] = value
    end
    if type(overrides) == "table" then
        for key, value in pairs(overrides) do
            merged[key] = value
        end
    end
    return merged
end

local function coverage_cfg(context)
    local cfg = runner_cfg(context)
    local from_runner = type(cfg.coverage) == "table" and cfg.coverage or {}
    local from_global = type(config) == "table" and type(config.coverage) == "table" and config.coverage
        or {}
    return shallow_merge(from_global, from_runner)
end

local function default_coverage_cfg(context)
    return {
        tool = "gcovr",
        report = "coverage/lcov.info",
        output = "coverage/teez.msgpack",
        reporter = "msgpack",
        clean_profile = true,
        configure_args = {
            "-DTEEZ_ENABLE_COVERAGE=ON",
            "-DCMAKE_BUILD_TYPE=Debug",
        },
    }
end

local function effective_coverage_cfg(context)
    return shallow_merge(default_coverage_cfg(context), coverage_cfg(context))
end

local function target_path(context)
    return context.target_path or "."
end

local function absolute_build_dir(target, dir)
    if dir:sub(1, 1) == "/" then
        return dir
    end
    return target .. "/" .. dir
end

local function resolve_source_dir(context)
    local target = target_path(context)
    local cfg = runner_cfg(context)
    if cfg.source_dir and cfg.source_dir ~= "" then
        return absolute_build_dir(target, cfg.source_dir)
    end
    return target
end

local function resolve_build_dir_path(context)
    local target = target_path(context)
    local cfg = runner_cfg(context)

    if fs.exists(target .. "/CTestTestfile.cmake") then
        return target
    end

    local configured_dir = cfg.build_dir
    if configured_dir and configured_dir ~= "" then
        return absolute_build_dir(target, configured_dir)
    end

    if fs.exists(target .. "/build/CTestTestfile.cmake") then
        return target .. "/build"
    end

    return absolute_build_dir(target, "build")
end

local function resolve_test_dir(context)
    local build = resolve_build_dir_path(context)
    if fs.exists(build .. "/CTestTestfile.cmake") then
        return build
    end

    local target = target_path(context)
    if fs.exists(target .. "/CTestTestfile.cmake") then
        return target
    end

    return build
end

local function append_flag(args, flag)
    table.insert(args, flag)
end

local function append_option(args, flag, value)
    if value == nil or value == false or value == "" then
        return
    end
    table.insert(args, flag)
    if value ~= true then
        table.insert(args, tostring(value))
    end
end

local function append_runner_args(args, context)
    local cfg = runner_cfg(context)
    if cfg.args then
        for _, arg in ipairs(cfg.args) do
            table.insert(args, arg)
        end
    end
end

local function ctest_args(context, trailing)
    local args = { "--test-dir", resolve_test_dir(context) }
    local cfg = runner_cfg(context)

    append_option(args, "-R", effective_filter(context, "regex"))
    append_option(args, "-E", effective_filter(context, "exclude"))
    append_option(args, "-L", effective_filter(context, "label"))
    append_option(args, "-LE", effective_filter(context, "exclude_label"))
    append_option(args, "-C", cfg_value(context, "config"))
    local parallel = cfg_value(context, "parallel")
    if parallel == nil and is_ci() then
        parallel = trim(sys.exec("nproc", {}))
    end
    append_option(args, "-j", parallel)
    append_option(args, "--timeout", cfg_value(context, "timeout"))
    append_option(args, "--parallel-level", cfg_value(context, "parallel_level"))
    append_option(args, "--resource-spec-file", cfg_value(context, "resource_spec_file"))

    if cfg.stop_on_failure then
        append_flag(args, "--stop-on-failure")
    end

    if cfg.no_output_log_tail then
        append_flag(args, "--no-output-log-tail")
    end

    if cfg.verbose then
        append_flag(args, "-V")
    end

    if cfg.repeat_until_fail then
        append_option(args, "--repeat", "until-fail:" .. tostring(cfg.repeat_until_fail))
    elseif cfg.repeat_mode then
        append_option(args, "--repeat", cfg.repeat_mode)
    end

    append_runner_args(args, context)

    if trailing then
        for _, flag in ipairs(trailing) do
            table.insert(args, flag)
        end
    end

    return args
end

local function cmake_lists_present(dir)
    return fs.exists(dir .. "/CMakeLists.txt")
end

local function ctest_tree_present(context)
    return fs.exists(resolve_test_dir(context) .. "/CTestTestfile.cmake")
end

local function needs_cmake_setup(context)
    if ctest_tree_present(context) then
        return false
    end
    return cmake_lists_present(resolve_source_dir(context))
end

local function should_configure(context)
    local cfg = runner_cfg(context)
    if cfg.auto_configure == false or cfg.configure == false then
        return false
    end
    if cfg.auto_configure == true or cfg.configure == true then
        return cmake_lists_present(resolve_source_dir(context))
    end
    return needs_cmake_setup(context)
end

local function should_build(context)
    local cfg = runner_cfg(context)
    if cfg.auto_build == false or cfg.build == false then
        return false
    end
    if cfg.auto_build == true or cfg.build == true then
        return cmake_lists_present(resolve_source_dir(context))
    end
    return needs_cmake_setup(context)
end

local function default_configure_args(cfg)
    if cfg.configure_args then
        return cfg.configure_args
    end
    if is_ci() then
        return { "-DCMAKE_BUILD_TYPE=RelWithDebInfo" }
    end
    return {}
end

local function default_build_args(cfg)
    if cfg.build_args then
        return cfg.build_args
    end

    local jobs = trim(os.getenv("CMAKE_BUILD_PARALLEL_LEVEL"))
    if jobs == "" then
        jobs = trim(os.getenv("CMAKE_BUILD_PARALLEL"))
    end
    if jobs == "" and is_ci() then
        jobs = trim(sys.exec("nproc", {}))
    end
    if jobs == "" then
        return nil
    end
    return { "-j", jobs }
end

local function append_args(args, extra)
    if not extra then
        return
    end
    for _, arg in ipairs(extra) do
        table.insert(args, arg)
    end
end

local function run_configure(context, extra_args)
    local cfg = runner_cfg(context)
    local source = resolve_source_dir(context)
    local build = resolve_build_dir_path(context)
    local args = { "-S", source, "-B", build }

    append_args(args, default_configure_args(cfg))
    append_args(args, extra_args)

    sys.exec("cmake", args)
end

local function run_build(context)
    local cfg = runner_cfg(context)
    local build = resolve_build_dir_path(context)
    local args = { "--build", build }

    local build_config = cfg.build_config or cfg.config
    if build_config and build_config ~= "" then
        table.insert(args, "--config")
        table.insert(args, build_config)
    end

    if cfg.build_target then
        table.insert(args, "--target")
        table.insert(args, cfg.build_target)
    end

    append_args(args, default_build_args(cfg))

    sys.exec("cmake", args)
end

local function ensure_cmake_ready(context, opts)
    opts = opts or {}
    local test_dir = resolve_test_dir(context)

    if should_configure(context) and not fs.exists(test_dir .. "/CTestTestfile.cmake") then
        local source = resolve_source_dir(context)
        if not cmake_lists_present(source) then
            error("ctest: configure requested but CMakeLists.txt not found in " .. source)
        end
        run_configure(context, opts.configure_args)
    end

    if should_build(context) then
        run_build(context)
    end
end

local function trim_test_name(name)
    return (name:gsub("%s+$", ""))
end

local function collect_test_names_from_ctest_output(output)
    local tests = {}

    for line in output:gmatch("[^\r\n]+") do
        local name = line:match("Test%s+#%d+:%s+(.+)")
        if name then
            table.insert(tests, trim_test_name(name))
        end
    end

    return tests
end

local function extract_tags(line)
    local tags = {}
    for tag in line:gmatch("%[([^%]]+)%]") do
        table.insert(tags, tag)
    end
    return tags
end

local function build_catch2_tree_index(executable)
    local output = sys.exec(executable, {"--list-tests"})
    local index = {}
    local pending_name = nil

    for line in output:gmatch("[^\r\n]+") do
        if line:match("^All available test cases:") then
            goto continue
        end

        local tags = extract_tags(line)
        if #tags > 0 and pending_name then
            index[pending_name] = table.concat(tags, "/") .. "/" .. pending_name
            pending_name = nil
            goto continue
        end

        local name = line:match("^  ([^ ].+)$")
        if name then
            pending_name = trim_test_name(name)
        end

        ::continue::
    end

    return index
end

local function unique_executables(test_entries)
    local executables = {}
    local ordered = {}

    for _, test in ipairs(test_entries or {}) do
        local executable = test.command and test.command[1]
        if executable and not executables[executable] then
            executables[executable] = true
            table.insert(ordered, executable)
        end
    end

    return ordered
end

local function fallback_tree_path(name)
    local head = name:match("^(%S+)")
    if head and head ~= name then
        return head .. "/" .. name
    end
    return name
end

local function executables_from_verbose_listing(context)
    local output = sys.exec("ctest", ctest_args(context, {"-N", "-V"}))
    local executables = {}
    local ordered = {}

    for line in output:gmatch("[^\r\n]+") do
        local executable = line:match("^Test command: (%S+)")
        if executable and not executables[executable] then
            executables[executable] = true
            table.insert(ordered, executable)
        end
    end

    return ordered
end

local function load_catch2_tree_indexes(context, test_entries)
    local indexes = {}
    local executables = unique_executables(test_entries)

    if #executables == 0 then
        executables = executables_from_verbose_listing(context)
    end

    for _, executable in ipairs(executables) do
        if fs.exists(executable) then
            local index = build_catch2_tree_index(executable)
            for name, path in pairs(index) do
                indexes[name] = path
            end
        end
    end

    return indexes
end

local function collect_tests(context)
    local json_output = sys.exec("ctest", ctest_args(context, {"--show-only=json-v1"}))
    local data = json.decode(json_output)
    local tests = {}

    for _, test in ipairs(data.tests or {}) do
        table.insert(tests, trim_test_name(test.name))
    end

    if #tests == 0 then
        local fallback_output = sys.exec("ctest", ctest_args(context, {"-N"}))
        tests = collect_test_names_from_ctest_output(fallback_output)
    end

    if context.command == "list" then
        local tree_indexes = load_catch2_tree_indexes(context, data.tests)
        for index, name in ipairs(tests) do
            tests[index] = tree_indexes[name] or fallback_tree_path(name)
        end
    end

    return tests
end

local function default_gcovr_filters(source, cov)
    if cov.gcovr_filters then
        return cov.gcovr_filters
    end

    local filters = {}
    for _, name in ipairs({ "src", "include", "lib" }) do
        if fs.exists(source .. "/" .. name) then
            table.insert(filters, name)
        end
    end

    if #filters == 0 then
        return { source }
    end

    return filters
end

local function gcovr_collect_args(source, test_dir, report, cov)
    local args = {
        "--root", source,
        "--object-directory", test_dir,
        "--gcov-ignore-errors=source_not_found",
        "--lcov", report,
    }

    local filters = default_gcovr_filters(source, cov)
    for _, filter in ipairs(filters) do
        if filter:sub(1, 1) == "/" or filter == source then
            table.insert(args, "--filter")
            table.insert(args, filter)
        else
            table.insert(args, "--filter")
            table.insert(args, source .. "/" .. filter:gsub("^/", ""))
        end
    end

    local excludes = cov.gcovr_excludes or {
        ".*/tests/.*",
        ".*/build/.*",
        ".*/_deps/.*",
        ".*/CMakeFiles/.*",
    }
    for _, exclude in ipairs(excludes) do
        table.insert(args, "--exclude")
        table.insert(args, exclude)
    end

    if cov.extra_gcovr_args then
        for _, arg in ipairs(cov.extra_gcovr_args) do
            table.insert(args, arg)
        end
    end

    return args
end

local function lcov_collect_args(test_dir, report, cov)
    local args = {
        "--capture",
        "--directory", test_dir,
        "--output-file", report,
        "--rc", "lcov_branch_coverage=1",
    }

    if cov.lcov_remove_patterns then
        for _, pattern in ipairs(cov.lcov_remove_patterns) do
            table.insert(args, "--remove")
            table.insert(args, report)
            table.insert(args, pattern)
            table.insert(args, "--output-file")
            table.insert(args, report)
        end
    end

    if cov.extra_lcov_args then
        for _, arg in ipairs(cov.extra_lcov_args) do
            table.insert(args, arg)
        end
    end

    return args
end

local function build_coverage_collect(context, source, test_dir, report, cov)
    local tool = cov.tool or "gcovr"
    if tool == "lcov" then
        return {
            command = "lcov",
            args = lcov_collect_args(test_dir, report, cov),
        }
    end

    return {
        command = "gcovr",
        args = gcovr_collect_args(source, test_dir, report, cov),
    }
end

local function clean_gcda_files(test_dir)
    sys.exec("find", { test_dir, "-name", "*.gcda", "-type", "f", "-delete" })
end

function list(context)
    return collect_tests(context)
end

function prefetch_starts()
    return false
end

function build_command(context)
    ensure_cmake_ready(context)
    return {
        command = "ctest",
        args = ctest_args(context, { "--output-on-failure" }),
    }
end

function build_coverage_run(context)
    local cov = effective_coverage_cfg(context)
    local source = resolve_source_dir(context)
    local test_dir = resolve_test_dir(context)

    ensure_cmake_ready(context, { configure_args = cov.configure_args })

    if cov.clean_profile ~= false then
        clean_gcda_files(test_dir)
    end

    local report = cov.report
    local output = cov.output
    local reporter = cov.reporter
    local min_line_rate = cov.min_line_rate

    return {
        command = "ctest",
        args = ctest_args(context, { "--output-on-failure" }),
        cwd = test_dir,
        collect = build_coverage_collect(context, source, test_dir, report, cov),
        report = report,
        reporter = reporter,
        output = output,
        min_line_rate = min_line_rate,
    }
end

local function test_id_from_line(line)
    local id = line:match("Test%s+#%d+:%s+(.-)%.%.%.")
    if id then
        return trim_test_name(id)
    end
    return nil
end

local function duration_ms_from_line(line)
    local seconds = line:match("(%d+%.%d+)%s+sec")
    if not seconds then
        return nil
    end
    return math.floor(tonumber(seconds) * 1000 + 0.5)
end

local function with_duration(event, line)
    local duration_ms = duration_ms_from_line(line)
    if duration_ms ~= nil then
        event.duration_ms = duration_ms
    end
    return event
end

function parse_line(line)
    local start_id = line:match("^%s*Start%s+%d+:%s+(.+)$")
    if start_id then
        return { event = "start", id = trim_test_name(start_id) }
    end

    local id = test_id_from_line(line)
    if not id then
        return nil
    end

    if line:find("***Failed", 1, true)
        or line:find("***Exception", 1, true)
        or line:find("***Timeout", 1, true)
    then
        local msg = line:match("%*%*%*(%w+)") or "Failed"
        return with_duration({ event = "fail", id = id, msg = msg }, line)
    end

    if line:find("***Not Run", 1, true) or line:find("***Skipped", 1, true) then
        return with_duration({ event = "skip", id = id }, line)
    end

    if line:find("Passed", 1, true) then
        return with_duration({ event = "pass", id = id }, line)
    end

    return nil
end
