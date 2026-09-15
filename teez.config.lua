local config = {
    profile = os.getenv("CI") and "ci" or "local",

    runners = {
        ctest = {
            build_dir = "build",
            exclude = "end-to-end",
        },
    },

    profiles = {
        ["local"] = {},
        ci = {
            runners = {
                ctest = {
                    exclude = "pytest plugin end-to-end",
                },
            },
        },
    },
}

return config
