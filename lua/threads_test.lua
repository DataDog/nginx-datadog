-- Content handler that tests light threads and various thread operations
ngx.log(ngx.DEBUG, "lua content handler started")

local results = {}

-- Function 1: Quick background task
local function quick_task()
    ngx.log(ngx.DEBUG, "lua light thread started")
    ngx.sleep(0.001)
    ngx.log(ngx.DEBUG, "lua light thread ended normally")
    return "quick_done"
end

-- Function 2: Slightly longer task
local function slow_task()
    ngx.log(ngx.DEBUG, "lua light thread started")
    ngx.sleep(0.01)
    ngx.log(ngx.DEBUG, "lua light thread ended normally")
    return "slow_done"
end

-- Function 3: Task that might be killed
local function killable_task()
    ngx.log(ngx.DEBUG, "lua light thread started")
    for i = 1, 100 do
        if i == 50 then
            ngx.log(ngx.DEBUG, "lua light thread midpoint")
        end
        ngx.sleep(0.0001)
    end
    ngx.log(ngx.DEBUG, "lua light thread ended normally")
    return "killable_done"
end

-- Spawn multiple threads
ngx.log(ngx.DEBUG, "lua spawning light threads")
local thread1 = ngx.thread.spawn(quick_task)
local thread2 = ngx.thread.spawn(slow_task)
local thread3 = ngx.thread.spawn(killable_task)

-- Wait for threads
local ok1, res1 = ngx.thread.wait(thread1)
ngx.log(ngx.DEBUG, "lua deleting light thread")
if ok1 then
    table.insert(results, "Thread 1: " .. res1)
end

local ok2, res2 = ngx.thread.wait(thread2)
ngx.log(ngx.DEBUG, "lua deleting light thread")
if ok2 then
    table.insert(results, "Thread 2: " .. res2)
end

-- Kill thread3 to test forcible cleanup
ngx.thread.kill(thread3)
ngx.log(ngx.DEBUG, "lua deleting light thread")
table.insert(results, "Thread 3: killed")

-- Output results
ngx.say("Light threads test completed")
for _, result in ipairs(results) do
    ngx.say(result)
end

ngx.log(ngx.DEBUG, "lua request cleanup: forcible=0")
