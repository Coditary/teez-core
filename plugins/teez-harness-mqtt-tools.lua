-- Harness plugin: mqtt-benchmark and mosquitto_sub wrappers

function register(api)
    local M = {}

    function M.publish(opts)
        if type(opts) ~= "table" then
            error("mqtt.publish requires options table")
        end

        local broker = opts.broker or "tcp://127.0.0.1:1883"
        local topic = opts.topic or "init"
        local total = opts.total or opts.count or 1
        local rate = opts.rate or opts.clients or 1
        local payload = opts.payload or "init"
        local adjusted_count = math.max(1, math.floor(total / rate))

        api.emit({
            action = "publish",
            broker = broker,
            topic = topic,
            total = total,
            rate = rate,
        })

        local result = sys.run("mqtt-benchmark", {
            "--broker", broker,
            "--topic", topic,
            "--count", tostring(adjusted_count),
            "--clients", tostring(rate),
            "--payload", payload,
            "--qos", tostring(opts.qos or 0),
        }, opts.run_options)

        if result.exit_code ~= 0 then
            error("mqtt-benchmark failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.capture(opts)
        opts = opts or {}
        local host = opts.host or "127.0.0.1"
        local port = tostring(opts.port or 1883)
        local topic = opts.topic or "init"
        local file = opts.file

        local args = {"-h", host, "-p", port, "-t", topic}
        if file then
            table.insert(args, "-F")
            table.insert(args, "@Y-@m-@d @H:@M:@S.@N ; %p")
            return api.process.spawn("mosquitto_sub", args, {
                capture = true,
            })
        end

        return api.process.spawn("mosquitto_sub", args, opts.spawn_options)
    end

    function M.record_topics(opts)
        if type(opts) ~= "table" or type(opts.topics) ~= "table" then
            error("mqtt.record_topics requires topics table")
        end

        local handles = {}
        for name, topic in pairs(opts.topics) do
            handles[name] = M.capture({
                host = opts.host,
                port = opts.port,
                topic = topic,
                spawn_options = opts.spawn_options,
            })
        end

        handles.stop_all = function()
            for _, handle in pairs(handles) do
                if type(handle) == "table" and handle.stop then
                    handle.stop()
                end
            end
        end

        api.defer(function()
            handles.stop_all()
        end)

        return handles
    end

    api.provide("mqtt-tools", M)
end
