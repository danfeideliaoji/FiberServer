#pragma once
#include<string>
#include<stdint.h>
#include<chrono>
#include<cstdio>
#include<memory>
#include<sstream>
#include<vector>
#include<list>
#include<fstream>
#include<map>
#include "mutex.h"
#include "singleton.h"
#include "util.h"
#include "FiberServer/thread.h"
#include "FiberServer/fiber.h"
//%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n 默认格式
//使用流式方式将日志级别level的日志写入到logger
#define FIBER_LOG_LEVEL(logger,level)\
    if(logger->getLevel()<=level) \
        FiberServer::LogEventWrap(FiberServer::LogEvent::ptr(new FiberServer::LogEvent(logger,level,\
        __FILE__,__LINE__,0, FiberServer::GetThreadId(),FiberServer::Fiber::GetFiberId(),time(0),FiberServer::Thread::GetName()))).getSS()
//GetThreadId()在util里t
//各种等级的宏
#define FIBER_LOG_DEBUG(logger) FIBER_LOG_LEVEL(logger, FiberServer::LogLevel::Level::DEBUG)
#define FIBER_LOG_INFO(logger) FIBER_LOG_LEVEL(logger, FiberServer::LogLevel::Level::INFO)
#define FIBER_LOG_WARN(logger) FIBER_LOG_LEVEL(logger, FiberServer::LogLevel::Level::WARN)
#define FIBER_LOG_ERROR(logger) FIBER_LOG_LEVEL(logger, FiberServer::LogLevel::Level::ERROR)
#define FIBER_LOG_FATAL(logger) FIBER_LOG_LEVEL(logger, FiberServer::LogLevel::Level::FATAL)
//各种等级的格式化宏 更建议用##VA_ARGS方式  避免无参调用时编译错误
#define FIBER_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        FiberServer::LogEventWrap(FiberServer::LogEvent::ptr(new FiberServer::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, FiberServer::GetThreadId(),\
                -1, time(0), "unknown"))).getEvent()->format(fmt,##__VA_ARGS__)
#define FIBER_LOG_FMT_DEBUG(logger, fmt, ...) FIBER_LOG_FMT_LEVEL(logger, FiberServer::LogLevel::Level::DEBUG, fmt,##__VA_ARGS__)                
#define FIBER_LOG_FMT_INFO(logger,fmt,...) FIBER_LOG_FMT_LEVEL(logger,FiberServer::LogLevel::Level::INFO,fmt,##__VA_ARGS__) 
#define FIBER_LOG_FMT_WARN(logger,fmt,...) FIBER_LOG_FMT_LEVEL(logger,FiberServer::LogLevel::Level::WARN,fmt,##__VA_ARGS__)
#define FIBER_LOG_FMT_ERROR(logger,fmt,...) FIBER_LOG_FMT_LEVEL(logger,FiberServer::LogLevel::Level::ERROR,fmt,##__VA_ARGS__)
#define FIBER_LOG_FMT_FATAL(logger,fmt,...) FIBER_LOG_FMT_LEVEL(logger,FiberServer::LogLevel::Level::FATAL,fmt,##__VA_ARGS__)
//获取主日志器
#define FIBER_LOG_ROOT() FiberServer::LoggerMgr::GetInstance()->getRoot()
//获取指定名称的日志器 如果没有则创建
#define FIBER_LOG_NAME(name) FiberServer::LoggerMgr::GetInstance()->getLogger(name)
namespace FiberServer{
class Logger;//前向声明
class LogLevel{
public:
    //日志等级
    enum Level{
        UNKNOW=0,
        DEBUG=1,
        INFO=2,
        WARN=3,
        ERROR=4,//错误
        FATAL=5//致命错误
    };
    static const char* ToString(LogLevel::Level level);//将日志级别转为字符串
    // 最好返回const char* 而不是stirng    
    static LogLevel::Level FromString(const std::string &str);
};

class LogEvent//日志信息 内容以流的形式写入
{
public:
    typedef std::shared_ptr<LogEvent> ptr;//logevent的智能指针
    LogEvent(std::shared_ptr<Logger>logger,
    LogLevel::Level level,
    const char* file,int32_t line,
    uint32_t elapse,//程序启动开始到现在的毫秒数
    uint32_t thread_id,uint32_t fiber_id,
    uint64_t time,const std::string &thread_name
    );
     // 返回文件名
    const char* getFile() const { return m_file;}
     //返回行号
    int32_t getLine() const { return m_line;}
     //返回耗时
    uint32_t getElapse() const { return m_elapse;}
     // 返回线程ID
    uint32_t getThreadId() const { return m_threadId;}
     //返回协程ID
    uint32_t getFiberId() const { return m_fiberId;}
    //返回时间
    uint64_t getTime() const { return m_time;}
     // 返回线程名称
    const std::string& getThreadName() const { return m_threadName;}
     //返回日志内容
    std::string getContent() const { return m_ss.str();}
     // 返回日志器     
    std::shared_ptr<Logger> getLogger() const { return m_logger;}
     // 返回日志级别
    LogLevel::Level getLevel() const { return m_level;}
    // 返回日志内容流
    std::stringstream& getSS(){ return m_ss;}
    void format(const char* fmt,...);//格式化写入日志内容
    void format(const char* fmt,va_list al);//上面的format将al传给该函数处理
private:
    /// 文件名
    const char* m_file = nullptr;
    /// 行号
    int32_t m_line = 0;
    /// 程序启动开始到现在的毫秒数
    uint32_t m_elapse = 0;
    /// 线程ID
    uint32_t m_threadId = 0;
    /// 协程ID
    uint32_t m_fiberId = 0;
    /// 时间戳
    uint64_t m_time = 0;
    /// 线程名称
    std::string m_threadName;
    /// 日志内容流
    std::stringstream m_ss;
    /// 日志器
    std::shared_ptr<Logger> m_logger;
    /// 日志等级
    LogLevel::Level m_level;    
};

class LogEventWrap{//日志事件包装器 主要是为了在析构时自动将日志事件写入日志器
public:
    LogEventWrap(LogEvent::ptr e);
    ~LogEventWrap();//析构时会调用日志器的log方法
    LogEvent::ptr getEvent() const { return m_event; }
    std::stringstream& getSS(){
        return m_event->getSS();
    }
private:
    LogEvent::ptr m_event;//日志事件
};

class LogFormatter{//日志格式化器 最复杂的类 负责将"%d %p{%Y-%m} %tn"之类的格式化字符串解析成具体的日志内容
public:
    typedef std::shared_ptr<LogFormatter> ptr;
//logformatter不需要锁是因为formatter在构造时就确定了 之后不会修改
    LogFormatter(const std::string &patten);
    void init();//关键函数 解析patten
    bool isError() const{return m_error;}
    const std::string& getPattern() const{return m_pattern;}//不提供修改类
    //要改的换直接换一个类 解耦 支持多个appender用一个formatter同时log
    std::string format(LogEvent::ptr event);
    std::ostream& format(std::ostream &ofs,LogEvent::ptr event);
    //日志格式化项
    
public:
    class FormatItem//具体的输出类 负责具体输出event的某个内容
    {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        virtual ~FormatItem(){}//这里不用纯虚函数是因为有些子类可能不需要重写析构函数
        virtual void format(std::ostream &os,LogEvent::ptr event)=0;
        //sylar的原版接口void format(std::ostream& os, Logger::ptr logger, Level level, LogEvent::ptr event)
        //因为level logger都可以从event中获取到 这里直接就简化了 大佬的logger传来传去我都怕循环引用
    };
private:
    std::string m_pattern="%Y-%m-%d{%H:%M:%S}";//日志格式模板 有默认格式
    std::vector<FormatItem::ptr> m_items;//存放具体的输出类
    bool m_error=false;//是否解析错误
};

class LogAppender{//输出器 输出的地方 负责提供具体流
friend class Logger;
public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef Spinlock MutexType;
    enum class Type{//输出器拥有的类别
        STDOUT=1,
        FILE=2
    };
    virtual ~LogAppender(){};
    virtual void log(LogLevel::Level level,LogEvent::ptr event)=0;//log里调用formatter
    virtual std::string toYamlString()=0;//将appender的一些配置信息转为YAML
    void setFormatter(LogFormatter::ptr formatter);
    LogFormatter::ptr getFormatter();
    LogLevel::Level getLevel(){
        MutexType::Lock lock(m_mutex);
        return m_level; }
    void setLevel(LogLevel::Level level ){
        MutexType::Lock lock(m_mutex);
        m_level=level;}
protected:
    MutexType m_mutex;
    LogFormatter::ptr m_formatter;
    bool m_hasFormatter=false;
    LogLevel::Level m_level = LogLevel::DEBUG;//默认DUBUG模式
};

class StdoutLogAppender:public LogAppender{//控制台输出
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    void log(LogLevel::Level level,LogEvent::ptr event)override;
    std::string toYamlString()override;
};

class FileLogAppender:public LogAppender{
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename);
    void log(LogLevel::Level level,LogEvent::ptr event)override;
    std::string toYamlString() override;
    bool reopen();
private:
    std::string m_filename;//文件路径
    std::ofstream m_filestream;//文件流
    // std::chrono::steady_clock::time_point m_lasttime;//上一次打开文件时间
    uint64_t m_lasttime;//这里还是用c风格好一点 因为event的时间为c风格
    //每三秒打开一次
};
class Logger{//日志器
friend class LoggerManage;
public:
    typedef std::shared_ptr<Logger> ptr;
    typedef Spinlock MutexType;
    Logger(const std::string& name="root");
    void log(LogLevel::Level level, LogEvent::ptr event);
    std::string toYamlString();//将日志器的配置信息转为YAML 包括appender
    void addAppender(LogAppender::Type,const std::string& filename="log.txt");
    //根据类型添加输出器
    void addAppender(LogAppender::ptr appender );
    void delAppender(LogAppender::ptr appender);
    void clearAppenders();
    //set get 一些函数
    void setFormatter(LogFormatter::ptr val);
    void setFormatter(const std::string& val);
    const std::string& getName()const{return m_name;}
    void setLevel(LogLevel::Level level){
        MutexType::Lock lock(m_mutex);
        m_level=level;}
    LogLevel::Level getLevel(){
         MutexType::Lock lock(m_mutex);
        return m_level;}
    std::string getPattern(){
         MutexType::Lock lock(m_mutex);
        return m_formatter->getPattern();
    }    
    //各种级别的日志输出函数 虽然感觉没啥用 因为宏时还是调用的log接口
    void debug(LogEvent::ptr event);
    void info(LogEvent::ptr event);
    void warn(LogEvent::ptr event);
    void error(LogEvent::ptr event);
    void fatal(LogEvent::ptr event);
private:
    MutexType m_mutex;
    std::string m_name;//日志器名称
    LogLevel::Level m_level=LogLevel::Level::DEBUG ;//日志器日志级别 默认dubug
    //主题logger的formatter有自带的格式 所以一般不用管
    LogFormatter::ptr m_formatter;//日志输出格式 默认%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n
    std::list<LogAppender::ptr> m_appenders;// 日志目标集合 用链表更好因为删除时后面不会拷贝
    Logger::ptr m_root;//主日志器
};
class LoggerManage{//日志管理器
public:
    typedef std::shared_ptr<LoggerManage> ptr;
    typedef Spinlock MutexType;
    LoggerManage();
    Logger::ptr getLogger(const std::string& name);//获取日志器 如果没有则创建新的
    Logger::ptr getRoot() const { return m_root;}
private:
    MutexType m_mutex;
    std::map<std::string,Logger::ptr> m_loggers;//日志器容器 logger可以没有对应的appender 当没有时 会使用root的
    Logger::ptr m_root;//主日志器
};
//单例并且静态类型 使用静态的原因是先初始化防止用时还没有 因为如果创建全局变量的话 顺序不确定 用静态防止其他全局变量用时表还没创建
typedef Singleton<LoggerManage> LoggerMgr;//日志管理器单例
}