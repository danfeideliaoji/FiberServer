#!/bin/bash
# 云存储系统 wrk 压力测试脚本
# 用法: bash run_all.sh [host]
# 示例: bash run_all.sh https://localhost
#       bash run_all.sh https://192.168.1.100

HOST=${1:-"https://localhost"}
THREADS=4
CONNS=100
DURATION=30s
DIR=$(cd "$(dirname "$0")" && pwd)

echo "=========================================="
echo " 目标: $HOST  线程: $THREADS  连接: $CONNS  时长: $DURATION"
echo "=========================================="

# echo ""
# echo ">>> [1/5] 注册接口 /api/register"
# wrk -t$THREADS -c$CONNS -d$DURATION -s "$DIR/register.lua" "$HOST/api/register"

echo ""
echo ">>> [1/5] 登录接口 /api/login"
wrk -t$THREADS -c$CONNS -d$DURATION -s "$DIR/login.lua" "$HOST/api/login"

echo ""
echo ">>> [2/5] 文件列表 /api/myfiles"
wrk -t$THREADS -c$CONNS -d$DURATION -s "$DIR/myfiles.lua" "$HOST/api/myfiles"

echo ""
echo ">>> [3/5] MD5秒传检查 /api/md5"
wrk -t$THREADS -c$CONNS -d$DURATION -s "$DIR/md5check.lua" "$HOST/api/md5"

echo ""
echo ">>> [4/5] 文件下载 /api/download"
wrk -t$THREADS -c$CONNS -d$DURATION -s "$DIR/download.lua" "$HOST/api/download"

echo ""
echo ">>> [5/5] 注册接口 /api/register (低压)"
wrk -t2 -c20 -d10s -s "$DIR/register.lua" "$HOST/api/register"

echo ""
echo "=========================================="
echo " 压测完成"
echo "=========================================="
