-- login.lua
-- 对应你的注册脚本，生成一模一样的动态账号进行登录测试
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

local counter = 0
local tid = 0

-- 初始化每个线程的唯一 ID
function setup(thread)
    thread:set("id", tid)
    tid = tid + 1
end

-- 每个线程启动时获取自己的 ID
function init(args)
    id = wrk.thread:get("id")
end

-- 构造请求
function request()
    counter = counter + 1
    
    -- 【关键】这里的 uid 生成规则必须和 register.lua 完全一致
    local uid = string.format("bench_t%d_%d", id, counter)
    
    -- 构造登录 JSON：账号是动态的，密码是注册时统一设置的 "bench123456"
    wrk.body = string.format('{"user":"%s","pwd":"bench123456"}', uid)
    
    -- 返回请求格式，确保路径是 /api/login
    return wrk.format(nil, "/api/login")
end