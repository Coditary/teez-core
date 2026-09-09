local profile = os.getenv("CI") and "ci" or "local"

return {
    profile = profile,

    runners = {
        ctest = {
            build_dir = "build",
            -- Schnelle lokale Läufe: Integrationstests überspringen (brauchen pytest/worker/fixtures).
            exclude = "end-to-end",
        },
    },

    profiles = {
        ci = {
            runners = {
                ctest = {
                    -- CI: volle Suite, aber kein pytest ohne Installation
                    exclude = "pytest plugin end-to-end",
                },
            },
        },
    },
}
