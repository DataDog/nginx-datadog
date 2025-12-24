-- Content handler that uses ngx.timer.* to trigger fake request logs
local count = 0

-- Timer callback function - this will create fake HTTP requests
local function timer_callback(premature)
    if premature then
        return
    end

    -- This timer execution creates a fake HTTP request context
    -- which triggers all the "fake request" log messages
    ngx.log(ngx.DEBUG, "timer callback executed")
    count = count + 1
end

-- Schedule multiple timers to trigger fake request creation
for i = 1, 5 do
    local ok, err = ngx.timer.at(0.001 * i, timer_callback)
    if not ok then
        ngx.log(ngx.ERR, "failed to create timer: ", err)
    end
end

-- Use ngx.timer.every for recurring timer (if available)
if ngx.timer.every then
    local ok, err = ngx.timer.every(0.1, function(premature)
        if premature then
            return
        end
        ngx.log(ngx.DEBUG, "recurring timer callback executed")
    end)
    if not ok then
        ngx.log(ngx.ERR, "failed to create recurring timer: ", err)
    end
end

ngx.say("Timer test: scheduled 5 timers")
ngx.say("Timers will execute in background and create fake request contexts")
