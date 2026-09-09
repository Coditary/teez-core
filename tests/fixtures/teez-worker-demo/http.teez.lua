test.describe("HTTP (mock)", function()
    test.it("serves a mocked response", function(t)
        local server = t.mock_http_server()
        server.route("GET", "/health", { status = 200, body = '{"status":"ok"}' })

        local resp = http.get(server.url .. "/health")
        t.assert_http_status(resp, 200)
        t.assert_contains(resp.body, "ok")
    end)

    test.it("returns 404 for unknown routes", function(t)
        local server = t.mock_http_server()
        server.route("GET", "/health", { status = 200, body = "ok" })

        local resp = http.get(server.url .. "/missing")
        t.assert_http_status(resp, 404)
    end)

    test.todo("future: proxy interception demo")
end)
