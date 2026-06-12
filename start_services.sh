#!/bin/bash

# 服务启动/关闭脚本

ACTION=$1

start() {
    echo "启动所有服务..."
    
    # MySQL
    echo "启动 MySQL..."
    if pgrep -x "mysqld" > /dev/null; then
        echo "  MySQL 已运行"
    else
        service mysql start
        sleep 2
    fi
    
    # FastDFS
    # echo "启动 FastDFS..."
    # if pgrep -x "fdfs_storaged" > /dev/null; then
    #     echo "  FastDFS Storage 已运行"
    # else
    #     /usr/bin/fdfs_storaged /etc/fdfs/storage.conf start
    #     sleep 1
    # fi
    
    if pgrep -x "fdfs_trackerd" > /dev/null; then
        echo "  FastDFS Tracker 已运行"
    else
        /usr/bin/fdfs_trackerd /etc/fdfs/tracker.conf start
        sleep 1
    fi
    
    # Nginx
    echo "启动 Nginx..."
    if pgrep -x "nginx" > /dev/null; then
        echo "  Nginx 已运行"
    else
        /usr/local/nginx/sbin/nginx
        sleep 1
    fi
    
    echo "所有服务启动完成"
}

stop() {
    echo "关闭所有服务..."
    
    # Nginx
    echo "关闭 Nginx..."
    if pgrep -x "nginx" > /dev/null; then
        /usr/local/nginx/sbin/nginx -s stop
    else
        echo "  Nginx 未运行"
    fi
    
    # FastDFS Storage 已禁用
    # if pgrep -x "fdfs_storaged" > /dev/null; then
    #     /usr/bin/fdfs_storaged /etc/fdfs/storage.conf stop
    # else
    #     echo "  FastDFS Storage 未运行"
    # fi
    
    if pgrep -x "fdfs_trackerd" > /dev/null; then
        /usr/bin/fdfs_trackerd /etc/fdfs/tracker.conf stop
    else
        echo "  FastDFS Tracker 未运行"
    fi
    
    # MySQL
    echo "关闭 MySQL..."
    if pgrep -x "mysqld" > /dev/null; then
        service mysql stop
    else
        echo "  MySQL 未运行"
    fi
    
    echo "所有服务关闭完成"
}

status() {
    echo "检查服务状态..."
    
    echo -n "MySQL: "
    pgrep -x "mysqld" > /dev/null && echo "运行中" || echo "未运行"
    
    echo -n "FastDFS Tracker: "
    pgrep -x "fdfs_trackerd" > /dev/null && echo "运行中" || echo "未运行"
    
    echo -n "Nginx: "
    pgrep -x "nginx" > /dev/null && echo "运行中" || echo "未运行"
}

case "$ACTION" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        stop
        sleep 2
        start
        ;;
    status)
        status
        ;;
    *)
        echo "用法: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac
