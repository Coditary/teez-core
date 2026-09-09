return {
    profile = os.getenv("CI") and "ci" or "local",
}
