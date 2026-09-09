#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <sol/sol.hpp>

#include "teez/core/lua_helpers.hpp"

TEST_CASE("fs.exists returns true for current directory", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return fs.exists(".")
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<bool>());
}

TEST_CASE("fs.exists returns false for missing path", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return fs.exists("/path/that/does/not/exist/teez")
    )");
    REQUIRE(result.valid());
    REQUIRE_FALSE(result.get<bool>());
}

TEST_CASE("json.decode parses object into lua table", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        local value = json.decode('{"name":"teez","count":2}')
        return value.name .. ":" .. tostring(value.count)
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<std::string>() == "teez:2");
}

TEST_CASE("sys.exec captures command output", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.exec("bash", {"-c", "echo hello-from-sys"})
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<std::string>().find("hello-from-sys") != std::string::npos);
}

TEST_CASE("sys.run returns exit code stdout and stderr separately", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.run("bash", {"-c", "echo stdout-line; echo stderr-line >&2; exit 7"})
    )");
    REQUIRE(result.valid());

    const sol::table table = result;
    REQUIRE(table.get<int>("exit_code") == 7);
    REQUIRE(table.get<std::string>("stdout").find("stdout-line") != std::string::npos);
    REQUIRE(table.get<std::string>("stderr").find("stderr-line") != std::string::npos);
}

TEST_CASE("sys.run auto_respond drives interactive setup scripts", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.run("bash", {"-c", [[
            read -r -p "Username: " username
            read -r -s -p "Password: " password
            echo
            read -r -p "Do you want to continue? [y/N] " answer
            echo "Setup finished successfully"
        ]]}, {
            auto_respond = {
                ["Username:"] = "admin\n",
                ["Password:"] = "geheim123\n",
                ["Do you want to continue? [y/N]"] = "y\n",
            },
            timeout = 5000,
        })
    )");
    REQUIRE(result.valid());

    const sol::table table = result;
    REQUIRE(table.get<int>("exit_code") == 0);
    REQUIRE(table.get<std::string>("stdout").find("Setup finished successfully") != std::string::npos);
}

TEST_CASE("fs.glob finds matching files in directory", "[lua_helpers]") {
    const auto temp_dir = std::filesystem::temp_directory_path() / "teez_lua_helpers_glob";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    std::ofstream(temp_dir / "match.teez.lua") << "-- test\n";

    sol::state lua;
    teez::core::register_lua_helpers(lua);
    lua["glob_dir"] = temp_dir.string();

    const auto result = lua.safe_script(R"(
        local files = fs.glob(glob_dir, "*.teez.lua")
        return #files == 1
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<bool>());
}

TEST_CASE("sys.exec applies custom environment variables", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.exec("printenv", {"TEEZ_EXEC_ENV"}, {
            env = { TEEZ_EXEC_ENV = "from-lua" }
        })
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<std::string>().find("from-lua") != std::string::npos);
}

TEST_CASE("sys.run expect_stdout accepts table steps", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.run("bash", {"-c", [[
            read -r -p "Enter password: " p1
            echo done
        ]]}, {
            expect_stdout = {
                { match = "Enter password:", respond = "secret\n" },
            },
            timeout = 5000,
        })
    )");
    REQUIRE(result.valid());

    const sol::table table = result;
    REQUIRE(table.get<int>("exit_code") == 0);
    REQUIRE(table.get<std::string>("stdout").find("done") != std::string::npos);
}

TEST_CASE("sys.run rejects auto_respond combined with expect_stdout", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    try {
        lua.safe_script(R"(
            return sys.run("bash", {"-c", "echo ok"}, {
                auto_respond = { prompt = "x\n" },
                expect_stdout = {
                    { match = "prompt", respond = "y\n" },
                },
            })
        )");
        FAIL("expected sys.run option validation error");
    } catch (const sol::error& err) {
        REQUIRE(std::string(err.what()).find("cannot combine auto_respond and expect_stdout") !=
                std::string::npos);
    }
}

TEST_CASE("sys.run timeout option terminates long running command", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        return sys.run("bash", {"-c", "sleep 2; echo finished"}, {
            timeout = 200,
        })
    )");
    REQUIRE(result.valid());

    const sol::table table = result;
    REQUIRE(table.get<int>("exit_code") == 124);
    REQUIRE(table.get<std::string>("stdout").find("finished") == std::string::npos);
}

TEST_CASE("filter.matches is available from lua helpers", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        local desc = {
            file = "tests/system/smoke.teez.lua",
            type = "system",
            suites = { "API" },
            name = "return 9999",
        }
        local filters = {
            types = { "system" },
            name_glob = "return 999*",
        }
        return filter.matches(desc, filters)
    )");

    REQUIRE(result.valid());
    REQUIRE(result.get<bool>());
}

TEST_CASE("json.encode serializes lua tables", "[lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        local encoded = json.encode({ name = "teez", count = 2 })
        local decoded = json.decode(encoded)
        return decoded.name .. ":" .. tostring(decoded.count == 2)
    )");
    REQUIRE(result.valid());
    REQUIRE(result.get<std::string>() == "teez:true");
}
