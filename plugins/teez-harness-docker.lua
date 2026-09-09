-- Harness plugin: docker compose lifecycle

local function compose_file(opts)
    return (opts and opts.file) or "docker-compose.yml"
end

function register(api)
    local M = {}

    function M.up(opts)
        opts = opts or {}
        local file = compose_file(opts)
        api.emit({ action = "up", file = file })
        local result = sys.run("docker", {"compose", "-f", file, "up", "-d"}, opts.run_options)
        if result.exit_code ~= 0 then
            error("docker compose up failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.down(opts)
        opts = opts or {}
        local file = compose_file(opts)
        api.emit({ action = "down", file = file })
        local result = sys.run("docker", {"compose", "-f", file, "down"}, opts.run_options)
        if result.exit_code ~= 0 then
            error("docker compose down failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.ready(opts)
        opts = opts or {}
        local host = opts.host or "127.0.0.1"
        local port = opts.port
        if port == nil then
            error("docker.ready requires port")
        end
        return api.probe["until"](function()
            return api.probe.tcp(host, port, { timeout_ms = 500 })
        end, opts.timeout or 60)
    end

    function M.logs(opts)
        opts = opts or {}
        local file = compose_file(opts)
        return api.process.spawn("docker", {"compose", "-f", file, "logs", "-f"}, opts.spawn_options)
    end

    api.provide("docker", M)
end
