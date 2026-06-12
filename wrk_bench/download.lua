-- wrk -t4 -c100 -d30s -s download.lua https://localhost/api/download
-- 注意: 需要替换为实际存在的用户名和文件名
wrk.method = "GET"

local counter = 0
local files = {"test1.txt", "test2.txt", "test3.txt"}

function request()
    counter = counter + 1
    local f = files[(counter % #files) + 1]
    local path = string.format("/api/download?user=testuser0&filename=%s", f)
    return wrk.format(nil, path)
end
