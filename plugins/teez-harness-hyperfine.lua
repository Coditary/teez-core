-- Harness plugin: hyperfine command benchmarking

local function ensure_commands(opts)
    if type(opts) ~= "table" then
        error("hyperfine options must be a table")
    end

    local commands = opts.commands or opts.command
    if type(commands) == "string" then
        commands = { commands }
    end
    if type(commands) ~= "table" or #commands == 0 then
        error("hyperfine requires at least one command (commands = { ... })")
    end

    return commands
end

local function temp_json_path()
    local base = os.getenv("TMPDIR") or "/tmp"
    return string.format("%s/teez-hyperfine-%d-%d.json", base, os.time(), math.random(100000))
end

local function ensure_parent_dir(path)
    local parent = path:match("^(.*)/[^/]+$")
    if parent and parent ~= "" then
        sys.run("mkdir", { "-p", parent })
    end
end

local function read_text(path)
    local handle = io.open(path, "r")
    if not handle then
        error("failed to read hyperfine export: " .. path)
    end
    local content = handle:read("*a")
    handle:close()
    return content
end

function register(api)
    local M = {}

    function M.run(opts)
        local commands = ensure_commands(opts)
        local args = {}

        if opts.warmup then
            table.insert(args, "--warmup")
            table.insert(args, tostring(opts.warmup))
        end
        if opts.runs then
            table.insert(args, "-r")
            table.insert(args, tostring(opts.runs))
        end
        if opts.min_runs then
            table.insert(args, "--min-runs")
            table.insert(args, tostring(opts.min_runs))
        end
        if opts.max_runs then
            table.insert(args, "--max-runs")
            table.insert(args, tostring(opts.max_runs))
        end
        if opts.prepare then
            table.insert(args, "--prepare")
            table.insert(args, opts.prepare)
        end
        if opts.setup then
            table.insert(args, "--setup")
            table.insert(args, opts.setup)
        end
        if opts.reference then
            table.insert(args, "--reference")
            table.insert(args, opts.reference)
        end

        local export_json = opts.export_json or temp_json_path()
        table.insert(args, "--export-json")
        table.insert(args, export_json)
        if opts.export_json then
            ensure_parent_dir(export_json)
        end

        if opts.export_markdown then
            table.insert(args, "--export-markdown")
            table.insert(args, opts.export_markdown)
            ensure_parent_dir(opts.export_markdown)
        end
        if opts.export_csv then
            table.insert(args, "--export-csv")
            table.insert(args, opts.export_csv)
            ensure_parent_dir(opts.export_csv)
        end

        for _, command in ipairs(commands) do
            table.insert(args, command)
        end

        api.emit({
            action = "benchmark",
            commands = #commands,
            export_json = export_json,
        })

        local result = sys.run("hyperfine", args, opts.run_options)
        if result.exit_code ~= 0 then
            error("hyperfine failed (exit " .. result.exit_code .. "): " .. result.stderr)
        end

        local report = json.decode(read_text(export_json))
        if not opts.keep_export and export_json ~= opts.export_json then
            os.remove(export_json)
        end

        local summary = {
            export_json = export_json,
            stdout = result.stdout,
            stderr = result.stderr,
            results = report.results or {},
            report = report,
        }

        function summary.mean(index)
            local entry = summary.results[index or 1]
            if entry == nil then
                error("hyperfine result index out of range: " .. tostring(index or 1))
            end
            return entry.mean
        end

        function summary.command(index)
            local entry = summary.results[index or 1]
            if entry == nil then
                error("hyperfine result index out of range: " .. tostring(index or 1))
            end
            return entry.command
        end

        function summary.fastest()
            local best = nil
            for _, entry in ipairs(summary.results) do
                if best == nil or entry.mean < best.mean then
                    best = entry
                end
            end
            return best
        end

        function summary.ratio(a, b)
            local left = summary.results[a or 1]
            local right = summary.results[b or 2]
            if left == nil or right == nil then
                error("hyperfine ratio requires two valid result indices")
            end
            return right.mean / left.mean
        end

        return summary
    end

    api.provide("hyperfine", M)
end
