-- teez ctest adapter

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

local function target_path(context)
    return context.target_path or "."
end

local function absolute_build_dir(target, dir)
    if dir:sub(1, 1) == "/" then
        return dir
    end
    return target .. "/" .. dir
end

local function resolve_test_dir(context)
    local target = target_path(context)
    local cfg = runner_cfg(context)

    if fs.exists(target .. "/CTestTestfile.cmake") then
        return target
    end

    local configured_dir = cfg.build_dir
    if configured_dir and configured_dir ~= "" then
        local candidate = absolute_build_dir(target, configured_dir)
        if fs.exists(candidate .. "/CTestTestfile.cmake") then
            return candidate
        end
    end

    if fs.exists(target .. "/build/CTestTestfile.cmake") then
        return target .. "/build"
    end

    return target
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

local function ctest_args(context, trailing)
    local args = { "--test-dir", resolve_test_dir(context) }
    local cfg = runner_cfg(context)

    append_option(args, "-R", effective_filter(context, "regex"))
    append_option(args, "-E", effective_filter(context, "exclude"))
    append_option(args, "-L", effective_filter(context, "label"))
    append_option(args, "-LE", effective_filter(context, "exclude_label"))
    append_option(args, "-C", cfg_value(context, "config"))
    append_option(args, "-j", cfg_value(context, "parallel"))
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

    if trailing then
        for _, flag in ipairs(trailing) do
            table.insert(args, flag)
        end
    end

    return args
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

function list(context)
    return collect_tests(context)
end

function build_command(context)
    return {
        command = "ctest",
        args = ctest_args(context, { "--output-on-failure" }),
    }
end

local function test_id_from_line(line)
    local id = line:match("Test%s+#%d+:%s+(.-)%.%.%.")
    if id then
        return trim_test_name(id)
    end
    return nil
end

function parse_line(line)
    local id = test_id_from_line(line)
    if not id then
        return nil
    end

    if line:find("***Failed", 1, true)
        or line:find("***Exception", 1, true)
        or line:find("***Timeout", 1, true)
    then
        local msg = line:match("%*%*%*(%w+)") or "Failed"
        return { event = "fail", id = id, msg = msg }
    end

    if line:find("***Not Run", 1, true) or line:find("***Skipped", 1, true) then
        return { event = "skip", id = id }
    end

    if line:find("Passed", 1, true) then
        return { event = "pass", id = id }
    end

    return nil
end
