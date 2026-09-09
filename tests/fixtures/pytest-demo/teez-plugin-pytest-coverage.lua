-- Example plugin: run an instrumented program and collect coverage.
-- Use with: teez run --with-coverage <path-with-this-plugin>

local function target_path(context)
    return context.target_path or "."
end

function list(context)
    return { "system coverage run" }
end

function build_command(context)
    local target = target_path(context)
    return {
        command = "pytest",
        args = { "-q", target },
    }
end

function parse_line(line)
    return nil
end

function build_coverage_run(context)
    local target = target_path(context)
    return {
        command = "pytest",
        args = {
            "-q",
            "--cov=.",
            "--cov-report=lcov:coverage.lcov",
            target,
        },
        cwd = target,
        report = "coverage.lcov",
        reporter = "msgpack",
        output = "coverage.msgpack",
        min_line_rate = config.coverage and config.coverage.min_line_rate,
    }
end
