-- Content handler that uses cosockets (TCP sockets)
local results = {}

-- First try unix socket to prometheus (will fail but trigger socket logs)
local sock1 = ngx.socket.tcp()
sock1:settimeout(100)

local ok1, err1 = sock1:connect("unix:/tmp/nginx/prometheus-nginx.socket")
if ok1 then
    sock1:close()
    table.insert(results, "Unix socket test: OK")
else
    table.insert(results, "Unix socket test: " .. (err1 or "failed"))
end

-- Now test regular TCP socket to backend
local sock = ngx.socket.tcp()

-- Set timeouts
sock:settimeout(1000)  -- 1 second connect timeout
sock:settimeouts(1000, 2000, 3000)  -- connect, send, read timeouts

-- Try to connect to backend
local ok, err = sock:connect("backend", 3000)

if ok then
    -- Send HTTP request
    local request = "GET /health HTTP/1.0\r\nHost: backend\r\n\r\n"
    local bytes, err = sock:send(request)

    if bytes then
        -- Read response
        local data, err = sock:receive("*a")
        if data then
            table.insert(results, "TCP socket test: " .. #data .. " bytes received")
        else
            table.insert(results, "TCP socket read error: " .. (err or "unknown"))
        end
    else
        table.insert(results, "TCP socket send error: " .. (err or "unknown"))
    end

    -- Close socket
    sock:close()
else
    table.insert(results, "TCP socket connect error: " .. (err or "unknown"))
end

-- Output all results
ngx.say("Socket tests completed:")
for _, result in ipairs(results) do
    ngx.say(result)
end
