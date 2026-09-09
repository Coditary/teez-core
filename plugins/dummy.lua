-- teez reference plugin (walking skeleton / shell demo)

function list(context)
    return {}
end

function build_command(context)
    local target = context.target_path or "."

    if fs.exists(target) and target:match("%.sh$") then
        return {
            command = "bash",
            args = { target }
        }
    end

    return {
        command = "echo",
        args = { "teez-dummy: " .. target }
    }
end

function parse_line(line)
    return {
        event = "output",
        text = "LUA SAGT: " .. line
    }
end
