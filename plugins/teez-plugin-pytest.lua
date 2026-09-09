-- teez pytest adapter

local function target_path(context)
    return context.target_path or "."
end

local function resolve_glob_hit(root, file)
    if file:sub(1, 1) == "/" then
        return file
    end
    return root .. "/" .. file
end

local function path_excluded(target, pattern)
    return filter.glob_match(target, pattern)
end

local function runner_targets(context)
    local target = target_path(context)
    if not context.runner or not context.runner.include then
        return { target }
    end

    local paths = {}
    local seen = {}
    for _, pattern in ipairs(context.runner.include) do
        for _, file in ipairs(fs.glob(target, pattern)) do
            local resolved = resolve_glob_hit(target, file)
            local excluded = false
            if context.runner.exclude then
                for _, exclude_pattern in ipairs(context.runner.exclude) do
                    if path_excluded(resolved, exclude_pattern) then
                        excluded = true
                        break
                    end
                end
            end
            if not excluded and not seen[resolved] then
                seen[resolved] = true
                table.insert(paths, resolved)
            end
        end
    end

    if #paths == 0 then
        return { target }
    end
    return paths
end

local function append_runner_args(args, context)
    if not context.runner then
        return
    end

    if context.runner.pattern then
        table.insert(args, "-k")
        table.insert(args, context.runner.pattern)
    end

    if context.runner.markers then
        table.insert(args, "-m")
        table.insert(args, context.runner.markers)
    end

    if context.runner.args then
        for _, arg in ipairs(context.runner.args) do
            table.insert(args, arg)
        end
    end
end

local function is_node_id(line)
    return line:match("^[%w%./_%-]+::[%w_%-]+$") ~= nil
end

local function collect_tests(context)
    local tests = {}
    for _, target in ipairs(runner_targets(context)) do
        local output = sys.exec("pytest", { "--collect-only", "-q", target })
        for line in output:gmatch("[^\r\n]+") do
            if is_node_id(line) then
                table.insert(tests, line)
            end
        end
    end
    return tests
end

function list(context)
    return collect_tests(context)
end

function build_command(context)
    local args = { "-v", "--tb=line" }
    append_runner_args(args, context)
    for _, target in ipairs(runner_targets(context)) do
        table.insert(args, target)
    end

    return {
        command = "pytest",
        args = args,
    }
end

function build_list_command(context)
    local args = { "--collect-only", "-q" }
    for _, target in ipairs(runner_targets(context)) do
        table.insert(args, target)
    end
    return {
        command = "pytest",
        args = args,
    }
end

function parse_line(line)
    local id, status = line:match("^(.-)%s+(PASSED)%s")
    if id then
        return { event = "pass", id = id }
    end

    id, status = line:match("^(.-)%s+(FAILED)%s")
    if id then
        return { event = "fail", id = id, msg = "FAILED" }
    end

    id, status = line:match("^(.-)%s+(ERROR)%s")
    if id then
        return { event = "fail", id = id, msg = "ERROR" }
    end

    id, status = line:match("^(.-)%s+(SKIPPED)%s")
    if id then
        return { event = "skip", id = id }
    end

    return nil
end
