#!/bin/bash

# 日志查看脚本

SERVICE=$1

# 日志路径
MYSQL_ERROR_LOG="/var/log/mysql/error.log"
MYSQL_SLOW_LOG="/var/log/mysql/mysql-slow.log"
NGINX_ACCESS_LOG="/usr/local/nginx/logs/access.log"
NGINX_ERROR_LOG="/usr/local/nginx/logs/error.log"
SYS_LOG="/var/log/syslog"

show_mysql() {
    echo "=== MySQL 错误日志 ==="
    if [ -f "$MYSQL_ERROR_LOG" ]; then
        tail -50 "$MYSQL_ERROR_LOG"
    else
        echo "日志文件不存在: $MYSQL_ERROR_LOG"
    fi
    
    echo ""
    echo "=== MySQL 慢查询日志 ==="
    if [ -f "$MYSQL_SLOW_LOG" ]; then
        tail -30 "$MYSQL_SLOW_LOG"
    else
        echo "日志文件不存在: $MYSQL_SLOW_LOG"
    fi
}

show_nginx_access() {
    echo "=== Nginx 访问日志 (最近50行) ==="
    if [ -f "$NGINX_ACCESS_LOG" ]; then
        tail -50 "$NGINX_ACCESS_LOG"
    else
        echo "日志文件不存在: $NGINX_ACCESS_LOG"
    fi
}

show_nginx_error() {
    echo "=== Nginx 错误日志 ==="
    if [ -f "$NGINX_ERROR_LOG" ]; then
        tail -50 "$NGINX_ERROR_LOG"
    else
        echo "日志文件不存在: $NGINX_ERROR_LOG"
    fi
}

show_nginx() {
    echo "=== Nginx 访问日志 (最近50行) ==="
    if [ -f "$NGINX_ACCESS_LOG" ]; then
        tail -50 "$NGINX_ACCESS_LOG"
    else
        echo "日志文件不存在: $NGINX_ACCESS_LOG"
    fi
    
    echo ""
    echo "=== Nginx 错误日志 ==="
    if [ -f "$NGINX_ERROR_LOG" ]; then
        tail -50 "$NGINX_ERROR_LOG"
    else
        echo "日志文件不存在: $NGINX_ERROR_LOG"
    fi
}

show_fdfs() {
    echo "=== FastDFS Tracker 日志 ==="
    if [ -f "/home/fastdfs/tracker/logs/trackerd.log" ]; then
        tail -50 "/home/fastdfs/tracker/logs/trackerd.log"
    elif [ -f "/home/a/fastdfs/logs/trackerd.log" ]; then
        tail -50 "/home/fastdfs/logs/trackerd.log"
    else
        echo "日志文件不存在"
    fi
    
    echo ""
    echo "=== FastDFS Storage 日志 ==="
    if [ -f "/var/log/fdfs_storaged.log" ]; then
        tail -50 "/var/log/fdfs_storaged.log"
    elif [ -f "/home/a/fastdfs/logs/storaged.log" ]; then
        tail -50 "/home/fastdfs/logs/storaged.log"
    else
        echo "日志文件不存在"
    fi
}

show_syslog() {
    echo "=== 系统日志 (最近50行) ==="
    if [ -f "$SYS_LOG" ]; then
        tail -50 "$SYS_LOG" | grep -i -E "mysql|nginx|fdfs|error"
    else
        echo "日志文件不存在: $SYS_LOG"
    fi
}

show_all() {
    echo "=========================================="
    echo "             所有日志概览"
    echo "=========================================="
    
    echo ""
    show_mysql
    
    echo ""
    echo "=========================================="
    
    echo ""
    show_nginx
}

follow_log() {
    local logfile=$1
    local name=$2
    echo "实时监控 $name 日志 (Ctrl+C 退出)..."
    tail -f "$logfile"
}

case "$SERVICE" in
    mysql)
        show_mysql
        ;;
    nginx-access)
        show_nginx_access
        ;;
    nginx-error)
        show_nginx_error
        ;;
    nginx)
        show_nginx
        ;;
    fdfs)
        show_fdfs
        ;;
    syslog)
        show_syslog
        ;;
    all)
        show_all
        ;;
    -f|--follow)
        echo "请指定服务: mysql | nginx-access | nginx-error | fdfs"
        echo "例如: $0 -f mysql"
        ;;
    *)
        echo "用法: $0 {mysql|nginx|nginx-access|nginx-error|fdfs|syslog|all}"
        echo ""
        echo "  mysql        - 查看 MySQL 日志"
        echo "  nginx        - 查看 Nginx 访问+错误日志"
        echo "  nginx-access - 仅查看 Nginx 访问日志"
        echo "  nginx-error  - 仅查看 Nginx 错误日志"
        echo "  fdfs         - 查看 FastDFS 日志"
        echo "  syslog       - 查看系统日志"
        echo "  all          - 查看所有日志"
        echo ""
        echo "示例:"
        echo "  $0 nginx-access  # 仅查看访问日志"
        echo "  $0 nginx-error   # 仅查看错误日志"
        ;;
esac
