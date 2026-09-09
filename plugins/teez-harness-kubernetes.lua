-- Harness plugin: kubernetes / minikube lifecycle

local function k8s_path(opts)
    if type(opts) ~= "table" or type(opts.path) ~= "string" or opts.path == "" then
        error("kubernetes deploy/teardown requires path")
    end
    return opts.path
end

function register(api)
    local function write_temp_manifest(manifest)
        local file = os.getenv("TMPDIR") or "/tmp"
        file = file .. "/teez-k8s-" .. tostring(os.time()) .. "-" .. tostring(math.random(100000)) .. ".yaml"
        local handle = io.open(file, "w")
        if not handle then
            error("failed to write temporary kubernetes manifest")
        end
        handle:write(manifest)
        handle:close()
        return file
    end

    local function apply_manifest(path, opts, action)
        local manifest = sys.exec("kubectl", {"kustomize", path})
        if opts.vars then
            manifest = api.vars.apply(manifest, opts.vars)
        end

        local temp_file = write_temp_manifest(manifest)
        local args
        if action == "delete" then
            args = {"delete", "--ignore-not-found", "-f", temp_file}
        else
            args = {"apply", "-f", temp_file}
        end

        local result = sys.run("kubectl", args, opts.run_options)
        os.remove(temp_file)
        return result
    end

    local M = {}

    function M.up(opts)
        opts = opts or {}
        local args = {"start"}
        if opts.driver then
            table.insert(args, "--driver=" .. opts.driver)
        end
        if opts.memory then
            table.insert(args, "--memory=" .. tostring(opts.memory))
        end
        if opts.cpus then
            table.insert(args, "--cpus=" .. tostring(opts.cpus))
        end
        api.emit({ action = "minikube_up" })
        local result = sys.run("minikube", args, opts.run_options)
        if result.exit_code ~= 0 then
            error("minikube start failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.down(opts)
        opts = opts or {}
        api.emit({ action = "minikube_down" })
        local result = sys.run("minikube", {"stop"}, opts.run_options)
        if result.exit_code ~= 0 then
            error("minikube stop failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.deploy(opts)
        local path = k8s_path(opts)
        api.emit({ action = "deploy", path = path })
        local result = apply_manifest(path, opts, "apply")
        if result.exit_code ~= 0 then
            error("kubectl apply failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.teardown(opts)
        local path = k8s_path(opts)
        api.emit({ action = "teardown", path = path })
        local result = apply_manifest(path, opts, "delete")
        if result.exit_code ~= 0 then
            error("kubectl delete failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    function M.ready(opts)
        opts = opts or {}
        local timeout = opts.timeout or 600
        return api.probe["until"](function()
            local result = sys.run("kubectl", {
                "wait",
                "--for=condition=ready",
                "pod",
                "--all",
                "--timeout=5s",
            })
            return result.exit_code == 0
        end, timeout)
    end

    function M.forward(opts)
        if type(opts) ~= "table" or type(opts.service) ~= "string" then
            error("kubernetes.forward requires service")
        end
        local mapping = opts.mapping or "8080:8080"
        return api.process.spawn("kubectl", {"port-forward", opts.service, mapping}, opts.spawn_options)
    end

    function M.metrics_start(opts)
        opts = opts or {}
        return api.capture.for_duration(opts.duration or 15, {
            every = opts.interval or 5,
            file = opts.file or "pod_performance.csv",
            header = opts.header or "Timestamp,Pod,CPU_m,Memory_Mi",
            tag = "kubernetes.metrics",
        }, function()
            local output = sys.exec("kubectl", {"top", "pods", "--no-headers"})
            return os.date("%H:%M:%S") .. "," .. output:gsub("%s+", " "):gsub(" ", ",", 2)
        end)
    end

    function M.fault_scale(opts)
        if type(opts) ~= "table" or type(opts.deployment) ~= "string" then
            error("kubernetes.fault_scale requires deployment")
        end
        local replicas = opts.replicas or 0
        api.emit({ action = "fault_scale", deployment = opts.deployment, replicas = replicas })
        local result = sys.run("kubectl", {
            "scale",
            "deployment/" .. opts.deployment,
            "--replicas=" .. tostring(replicas),
        })
        if result.exit_code ~= 0 then
            error("kubectl scale failed (exit " .. result.exit_code .. ")")
        end
        return result
    end

    api.provide("kubernetes", M)
end
