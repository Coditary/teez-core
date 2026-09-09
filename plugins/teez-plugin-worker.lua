-- teez-worker orchestrator plugin

local function target_path(context)
    return context.target_path or "."
end

local function apply_runner_env(spec, context)
    if not context.runner then
        return
    end

    spec.env = spec.env or {}
    spec.env.TEEZ_RUNNER_OPTIONS = json.encode(context.runner)
end

local function worker_list_args(target)
    local args = {}
    if teez.worker_subcommand then
        args[#args + 1] = teez.worker_subcommand
    end
    args[#args + 1] = "--list"
    args[#args + 1] = target
    return args
end

function list(context)
    local target = target_path(context)
    local worker_bin = teez.worker_bin or "teez-worker"
    local args = worker_list_args(target)
    local options = nil

    if context.filters then
        options = {
            env = {
                TEEZ_FILTERS = json.encode(context.filters),
            },
        }
    end

    local output
    if options then
        output = sys.exec(worker_bin, args, options)
    else
        output = sys.exec(worker_bin, args)
    end

    local tests = {}
    for line in output:gmatch("[^\r\n]+") do
        if line ~= "" then
            table.insert(tests, line)
        end
    end
    return tests
end

function build_command(context)
    local target = target_path(context)
    local worker_bin = teez.worker_bin or "teez-worker"
    local args = {}
    if teez.worker_subcommand then
        args[#args + 1] = teez.worker_subcommand
    end
    args[#args + 1] = target
    local spec = {
        command = worker_bin,
        args = args,
    }

    if context.filters then
        spec.env = {
            TEEZ_FILTERS = json.encode(context.filters),
        }
    end

    apply_runner_env(spec, context)
    return spec
end

function build_list_command(context)
    local target = target_path(context)
    local worker_bin = teez.worker_bin or "teez-worker"
    local spec = {
        command = worker_bin,
        args = worker_list_args(target),
    }
    apply_runner_env(spec, context)
    return spec
end

function parse_line(line)
    if line:sub(1, 1) ~= "{" then
        return nil
    end
    return json.decode(line)
end
