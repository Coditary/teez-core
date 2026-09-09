-- Harness plugin: local process control

local function command_args(opts)
    if type(opts) ~= "table" then
        error("process options must be a table")
    end
    if type(opts.command) ~= "string" or opts.command == "" then
        error("process options require command")
    end
    return opts.command, opts.args or {}
end

function register(api)
    local M = {}

    function M.run(opts)
        local command, args = command_args(opts)
        local result = sys.run(command, args, opts.run_options)
        if result.exit_code ~= 0 then
            error(string.format("process.run failed: %s (exit %d)", command, result.exit_code))
        end
        return result
    end

    function M.spawn(opts)
        local command, args = command_args(opts)
        local handle = api.process.spawn(command, args, opts.spawn_options)
        api.defer(function()
            handle.stop()
        end)
        return handle
    end

    function M.start(opts)
        return M.spawn(opts)
    end

    api.provide("process", M)
end
