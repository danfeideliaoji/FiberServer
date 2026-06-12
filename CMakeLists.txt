cmake_minimum_required(VERSION 3.10)
project(FiberServer)

# --- 1. 基础编译设置 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# 定义一个开关，
option(ENABLE_ASAN "Enable AddressSanitizer" off)
option(USE_SOCI_DB "Use SOCI database backend" off)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    # 添加编译选项
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    # 添加链接选项（ASan 需要在链接阶段也加入 flag）
    add_link_options(-fsanitize=address)
endif()



# --- 2. 依赖库查找 ---
# 确保 vcpkg 路径已通过命令行 -DCMAKE_TOOLCHAIN_FILE 传入，此处不再手动 set PREFIX_PATH
find_package(Threads REQUIRED)
find_package(Boost REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

if(USE_SOCI_DB)
    find_package(SOCI CONFIG REQUIRED)
endif()

if(NOT TARGET yaml-cpp::yaml-cpp AND TARGET yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

# 自动查找 MySQL (vcpkg 环境下通常能直接找到)
find_package(unordered_mysql QUIET) # 尝试查找，若失败可改用系统路径

# --- 3. 源码列表 ---
set(LIB_SRCS 
    FiberServer/base/mutex.cpp FiberServer/base/util.cpp FiberServer/base/log.cpp FiberServer/base/config.cpp
    FiberServer/thread.cpp FiberServer/fiber.cpp FiberServer/scheduler.cpp FiberServer/time.cpp
    FiberServer/stream.cpp FiberServer/fd_manager.cpp FiberServer/iomanager.cpp FiberServer/hook.cpp
    FiberServer/net/address.cpp FiberServer/net/socket.cpp FiberServer/net/bytearray.cpp
    FiberServer/net/socket_stream.cpp FiberServer/net/tcp_server.cpp
    FiberServer/net/http/http.cpp FiberServer/net/http/http11_parser.cpp FiberServer/net/http/httpclient_parser.cpp
    FiberServer/net/http/http_parser.cpp FiberServer/net/http/http_session.cpp 
    FiberServer/net/http/servlet.cpp FiberServer/net/http/http_server.cpp
    FiberServer/servlets/config_servlet.cpp FiberServer/servlets/status_servlet.cpp FiberServer/servlets/login_servlet.cpp FiberServer/servlets/register_servlet.cpp
    FiberServer/servlets/chunkupload_servlet.cpp FiberServer/servlets/dirupload_servlet.cpp FiberServer/servlets/upload_servlet.cpp
    FiberServer/servlets/myfiles_servlet.cpp FiberServer/servlets/md5_servlet.cpp FiberServer/servlets/download_servlet.cpp
    FiberServer/servlets/deletefile_servlet.cpp
    FiberServer/db/mysql.cpp FiberServer/util/json_util.cpp FiberServer/util/hash_util.cpp
    FiberServer/my/mysqlop.cpp FiberServer/my/chunkManager.cpp FiberServer/my/fastdfs.cpp
    FiberServer/my/application.cpp
)

if(USE_SOCI_DB)
    list(APPEND LIB_SRCS
        FiberServer/db/soci_db.cpp
        FiberServer/my/mysqlop_soci.cpp
    )
endif()

# --- 4. 目标配置 ---
add_library(fiberserver SHARED ${LIB_SRCS})
target_include_directories(fiberserver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(USE_SOCI_DB)
    target_compile_definitions(fiberserver PUBLIC FIBERSERVER_USE_SOCI)
endif()



# --- 6. 链接库 ---
target_link_libraries(fiberserver
    PUBLIC 
        yaml-cpp::yaml-cpp
        Threads::Threads
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
        JsonCpp::JsonCpp
        fdfsclient
        mysqlclient  # 推荐直接使用库名，vcpkg 会自动处理路径
        z zstd       # 压缩依赖
)

# --- 7. 可执行文件 ---
if(USE_SOCI_DB)
    set(SOCI_LINK_TARGETS)
    if(TARGET SOCI::soci_core)
        list(APPEND SOCI_LINK_TARGETS SOCI::soci_core)
    elseif(TARGET SOCI::core)
        list(APPEND SOCI_LINK_TARGETS SOCI::core)
    else()
        list(APPEND SOCI_LINK_TARGETS soci_core)
    endif()

    if(TARGET SOCI::soci_mysql)
        list(APPEND SOCI_LINK_TARGETS SOCI::soci_mysql)
    elseif(TARGET SOCI::mysql)
        list(APPEND SOCI_LINK_TARGETS SOCI::mysql)
    else()
        list(APPEND SOCI_LINK_TARGETS soci_mysql)
    endif()

    target_link_libraries(fiberserver PUBLIC ${SOCI_LINK_TARGETS})
endif()

add_executable(test FiberServer/tests/test.cpp) 
target_link_libraries(test fiberserver)

add_executable(server_main FiberServer/tests/server_main.cpp)
target_link_libraries(server_main fiberserver)
