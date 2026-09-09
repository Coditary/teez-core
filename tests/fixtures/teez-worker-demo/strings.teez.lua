test.describe("Strings", function()
    test.each({
        { "foo", "foo" },
        { "bar", "bar" },
        { "teez", "teez" },
    })("assert_eq(%s, %s)", function(t, left, right)
        t.assert_eq(left, right)
    end)

    test.it("assert_in finds values in tables", function(t)
        t.assert_in({ "red", "green", "blue" }, "green")
        t.assert_in({ id = 42, name = "teez" }, "teez")
    end)

    test.it("assert_type checks lua types", function(t)
        t.assert_type("hello", "string")
        t.assert_type(42, "number")
        t.assert_type({}, "table")
    end)
end)
