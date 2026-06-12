-- wrk -t1 -c10 -d10s -s deletefile.lua https://localhost/api/deletefile
-- 注意: 删除操作有副作用，建议低并发测试
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

local counter = 0

function request()
    counter = counter + 1
    wrk.body = string.format('{"user":"benchuser","file_name":"bench_file_%d.dat"}', counter)
    return wrk.format(nil, "/api/deletefile")
end
