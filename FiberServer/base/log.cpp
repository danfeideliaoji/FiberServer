#include "log.h"
#include <stdarg.h>
#include <string>
#include <vector>
#include <time.h>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <tuple>
#include <yaml-cpp/yaml.h>
#include <functional>
#include "config.h"
namespace FiberServer
{
    const char* LogLevel::ToString(LogLevel::Level level)
    {
        switch (level)
        { // 用宏简化代码
#define XX(level)         \
    case LogLevel::level: \
        return #level;
            XX(DEBUG);
            XX(INFO);
            XX(WARN);
            XX(ERROR);
            XX(FATAL);
        default:
            return "UNKNOW";
#undef XX
        }
    }
    LogLevel::Level LogLevel::FromString(const std::string &str)
    {
#define XX(level, v)            \
    if (str == #v)              \
    {                           \
        return LogLevel::level; \
    }
        XX(DEBUG, debug);
        XX(INFO, info);
        XX(WARN, warn);
        XX(ERROR, error);
        XX(FATAL, fatal);
        XX(DEBUG, DEBUG);
        XX(INFO, INFO);
        XX(WARN, WARN);
        XX(ERROR, ERROR);
        XX(FATAL, FATAL);
        return LogLevel::UNKNOW;
#undef XX
    }
    LogEvent::LogEvent(std::shared_ptr<Logger> logger,
                       LogLevel::Level level,
                       const char *file, int32_t line,
                       uint32_t elapse, // 程序启动开始到现在的毫秒数
                       uint32_t thread_id, uint32_t fiber_id,
                       uint64_t time, const std::string &thread_name) : m_logger(logger), m_level(level), m_file(file),
                                                                        m_line(line), m_elapse(elapse),m_threadId(thread_id),
                                                                        m_fiberId(fiber_id), m_time(time), m_threadName(thread_name)
    {
    }
    void LogEvent::format(const char *fmt, ...)
    {
        va_list al;
        va_start(al, fmt);
        format(fmt, al);
        va_end(al);
    }
    void LogEvent::format(const char *fmt, va_list al)
    {
        char *buf = nullptr;
        int len = vasprintf(&buf, fmt, al); //vsnprintf 是专门接收 va_list 的 printf 版本
        if (len != -1)
        {
            m_ss << std::string(buf, len);
            free(buf); // 记得释放内存 因为vasprintf分配的内存是在堆上
        }
    }
    LogEventWrap::LogEventWrap(LogEvent::ptr e) : m_event(e) {}

    // 析构时将日志写入日志器
    //  核心函数 非常非常精巧 析构时自动调用日志器的log方法
    LogEventWrap::~LogEventWrap()
    {
        m_event->getLogger()->log(m_event->getLevel(), m_event);
    }
    LogFormatter::LogFormatter(const std::string &pattern)
        : m_pattern(pattern)
    {
        if (m_pattern.empty())
        { // 防止空格式化字符串
            m_pattern = "%Y-%m-%d{%H:%M:%S}";
        }
        init();
    }
    // 具体的FormatItem子类实现 负责各个字段的格式化
    // 构造函数都有参数是为了保持接口统一 因为有些格式化要参数
    //  用ostream 是因为传入的可以是任何输出流 比如文件流 控制台流等
    class FilenameFormatItem : public LogFormatter::FormatItem
    {
    public:
        FilenameFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getFile();
        }
    };
    class LineFormatItem : public LogFormatter::FormatItem
    {
    public:
        LineFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getLine();
        }
    };
    class NewLineFormatItem : public LogFormatter::FormatItem
    {
    public:
        NewLineFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event)
        {
            os << std::endl;
        }
    };
    class StringFormatItem : public LogFormatter::FormatItem
    {
    public:
        StringFormatItem(const std::string &str) : m_string(str) {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << m_string;
        }

    private:
        std::string m_string;
    };
    class MessageFormatItem : public LogFormatter::FormatItem
    {
    public:
        MessageFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getContent();
        }
    };
    class LevelFormatItem : public LogFormatter::FormatItem
    {
    public:
        LevelFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << LogLevel::ToString(event->getLevel());
        }
    };
    class ElapseFormatItem : public LogFormatter::FormatItem
    {
    public:
        ElapseFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getElapse();
        }
    };
    class NameFormatItem : public LogFormatter::FormatItem
    {
    public:
        NameFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getLogger()->getName();
        }
    };
    class ThreadIdFormatItem : public LogFormatter::FormatItem
    {
    public:
        ThreadIdFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getThreadId();
        }
    };
    class FiberIdFormatItem : public LogFormatter::FormatItem
    {
    public:
        FiberIdFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getFiberId();
        }
    };
    class ThreadNameFormatItem : public LogFormatter::FormatItem
    {
    public:
        ThreadNameFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << event->getThreadName();
        }
    };
    class TabFormatItem : public LogFormatter::FormatItem
    {
    public:
        TabFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            os << "\t";
        }
    };
    class DateTimeFormatItem : public LogFormatter::FormatItem
    {
    public:
        DateTimeFormatItem(const std::string &format="")
        {
            if (format.empty())
            {
                m_format = "%Y-%m-%d %H:%M:%S";
            }
            else
            {
                m_format = format;
            }
        }
        // 格式化输出时间
        void format(std::ostream &os, LogEvent::ptr event) override
        {
            time_t time = event->getTime();
            struct tm tm;
            localtime_r(&time, &tm);
            char buf[64];
            strftime(buf, sizeof(buf), m_format.c_str(), &tm);
            os << buf;
        }

    private:
        std::string m_format; // 默认值在LogFormatter里指定
    };
    void LogFormatter::init()
    {
        /*解析patten 将%d %p{%Y-%m} %t %n之类的格式化字符串解析成具体的日志内容
        与sylar不同的是 不支持%dp 这类的贪婪解析多个 只能单个单个解析 %d{} %p这样的
        但好处是简单易懂
        */
        std::vector<std::tuple<std::string, std::string, int>> vec;
        // 存放解析结果 三元组 第一个元素是字符串 第二个元素是格式化参数 第三个元素表示是否是字符串
        std::string normalstr; // 存放普通字符串
        for (size_t i = 0; i < m_pattern.size(); ++i)
        {
            //  处理普通字符
            if (m_pattern[i] != '%')
            {
                normalstr.append(1, m_pattern[i]);
                continue;
            }
            //  处理 %% 转义
            if (i + 1 < m_pattern.size() && m_pattern[i + 1] == '%')
            {
                normalstr.append(1, '%');
                i += 1;//这里是+1 是因为结束后还会再+1
                continue;
            }
            // 开始解析
            // 结算之前的普通字符串
            if (!normalstr.empty())
            {
                vec.emplace_back(std::move(normalstr), "", 0);
                normalstr.clear();
            }
            //  极简解析逻辑
            size_t n = i + 1;

            // % 后面没东西，或者不是字母
            if (n >= m_pattern.size())
            {
                vec.emplace_back("%", "", 0); // 这里的 % 当作普通字符
                i=n;
                continue;
            }
            // 处理%后非字母的情况
            if (!isalpha(m_pattern[n]))//isalpha是用来判断是否为字母的 如果不为字母
            {
                normalstr.append(1,'%'); // 这里选择将%当作普通字符处理
                normalstr.append(1,m_pattern[n]);
                i=n;
                continue;                   
            }
            std::string key(1, m_pattern[n]);//记录对应的格式化类型
            std::string param; // 格式化参数
            n++;
            if (n < m_pattern.size() && m_pattern[n] == '{')
            {
                // 找 }
                size_t k = m_pattern.find('}', n);
                if (k == std::string::npos)
                {
                    // 没找到}
                    std::cout << "pattern error: missing '}' after " << key << "{" << std::endl;
                    vec.emplace_back("<<error>>", "", 0);//不管错误的了
                    m_error = true;
                    // 跳过非法段
                    i = n;
                    continue;
                }
                param = m_pattern.substr(n + 1, k - n - 1); // 截取格式化参数
                n = k + 1;//为了下面统一 修正了下
            }
            vec.emplace_back(key, param, 1);
            i = n - 1; //指向处理完的位置
        }
        //没处理完的情况
        if (!normalstr.empty())
        {
            vec.emplace_back(std::move(normalstr), "", 0);
        }
        // 映射表 将标识符映射到具体的FormatItem子类 用静态变量避免每次init都创建
        static std::unordered_map<std::string,
                                  std::function<LogFormatter::FormatItem::ptr(const std::string &str)>>
            s_format_items =
                {
// 好好学学大佬的宏 简化代码
#define XX(str, C)                                                                             \
    {                                                                                          \
        #str, [](const std::string &fmt) { return LogFormatter::FormatItem::ptr(new C(fmt)); } \
    } //%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
                    XX(m, MessageFormatItem),   // m:消息
                    XX(p, LevelFormatItem),     // p:日志级别
                    XX(r, ElapseFormatItem),    // r:累计毫秒数
                    XX(c, NameFormatItem),      // c:日志名称
                    XX(t, ThreadIdFormatItem),  // t:线程id
                    XX(n, NewLineFormatItem),   // n:换行
                    XX(d, DateTimeFormatItem),  // d:时间
                    XX(f, FilenameFormatItem),  // f:文件名
                    XX(l, LineFormatItem),      // l:行号
                    XX(T, TabFormatItem),       // T:Tab
                    XX(F, FiberIdFormatItem),   // F:协程id
                    XX(N, ThreadNameFormatItem) // N:线程名称
#undef XX
                };
        // 下面是将对应的格式放到m_items里面
        for (auto &i : vec)
        {
            if (std::get<2>(i) == 0)//普通字符
            {
                m_items.emplace_back(
                    LogFormatter::FormatItem::ptr(new StringFormatItem(std::get<0>(i))));
            }
            else if (std::get<2>(i) == 1)//需要格式化的字符
            {
                auto f = s_format_items.find(std::get<0>(i));//
                if (f == s_format_items.end())//没有该字符
                {
                    m_items.emplace_back(
                        LogFormatter::FormatItem::ptr(new StringFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
                    m_error = true;
                    continue;
                }
                m_items.emplace_back(s_format_items[std::get<0>(i)](std::get<1>(i)));// 得到对应的子类
            }
        }
    }
    std::string LogFormatter::format(LogEvent::ptr event)
    {
        std::stringstream ss;
        for (auto &i : m_items)
        {
            i->format(ss, event);
        }
        return ss.str();
    }
    std::ostream &LogFormatter::format(std::ostream &ss, LogEvent::ptr event)
    {
        for (auto &i : m_items)
        {
            i->format(ss, event);
        }
        return ss;
    }
    void LogAppender::setFormatter(LogFormatter::ptr formatter)
    {
        MutexType::Lock lock(m_mutex);
        m_formatter = formatter;
        m_hasFormatter = true;
    }
    LogFormatter::ptr LogAppender::getFormatter()
    {
        MutexType::Lock lock(m_mutex);
        return m_formatter;
    }
    void StdoutLogAppender::log(LogLevel::Level level, LogEvent::ptr event)
    {
        MutexType::Lock lock(m_mutex);
        if (level >= m_level) // 只输出大于等于自身等级的日志
            m_formatter->format(std::cout, event);
    }
    // 将appender的一些配置信息转为YAML
    std::string StdoutLogAppender::toYamlString()
    {
        YAML::Node node;
        node["type"] = "StdoutLogAppender";
        {
            MutexType::Lock lock(m_mutex); // 别忘了加锁
            if (m_level != LogLevel::Level::UNKNOW)
            {
                node["level"] = LogLevel::ToString(m_level);
            }
            if (m_hasFormatter && m_formatter)
            {
                node["formatter"] = m_formatter->getPattern();
            }
            else//没有则表明为默认的 就不特意写了
            {
                node["formatter"] = "";
            }
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
    FileLogAppender::FileLogAppender(const std::string &filename) : m_filename(filename)
    {
        reopen();
    }
    // 将appender的一些配置信息转为YAML
    std::string FileLogAppender::toYamlString()
    {
        YAML::Node node;
        node["type"] = "FileLogAppender";
        {
            MutexType::Lock lock(m_mutex);
            if (m_level != LogLevel::Level::UNKNOW)
            {
                node["level"] = LogLevel::ToString(m_level);
            }
            if (m_hasFormatter && m_formatter)
            {
                node["formatter"] = m_formatter->getPattern();
            }
            else
            {
                node["formatter"] = "";
            }
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
    bool FileLogAppender::reopen()
    {
        if (m_filestream.is_open())
        {
            m_filestream.close();
        }
        m_filestream.open(m_filename, std::ios::app);
        return m_filestream.is_open(); // 后续升级 比如文件轮转之类的
    }
    void FileLogAppender::log(LogLevel::Level level, LogEvent::ptr event)
    {
        MutexType::Lock lock(m_mutex);
        if (level >= m_level)
        {
            uint64_t now = event->getTime();
            if (now > m_lasttime + 3)
            { // 每三秒打开一次
                m_lasttime = now;
                reopen();
            }
            m_formatter->format(m_filestream, event);
        }
    }
    Logger::Logger(const std::string &name)
    {
        m_name = name;
        m_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n")); // 默认格式
    }
    void Logger::log(LogLevel::Level level, LogEvent::ptr event)
    {
        MutexType::Lock lock(m_mutex);
        if (level >= m_level)
        {
            if (!m_appenders.empty())
            {
                for (auto &i : m_appenders)
                {
                    i->log(level, event);
                }
            }
            else
            {
                m_root->log(level, event); // 如果只指定日志 但这个日志没有输出器的话 那只能用root的了
                // root有logmanage给logger
            }
        }
    }
    std::string Logger::toYamlString()
    {
        YAML::Node node;
        node["name"] = m_name;
        if (m_level != LogLevel::Level::UNKNOW)
        {
            node["level"] = LogLevel::ToString(m_level);
        }
        if(m_formatter!=nullptr){
            node["formatter"]=m_formatter->getPattern();
        }
        for (auto &i : m_appenders)
        {
            node["appenders"].push_back(YAML::Load(i->toYamlString()));//这里push_back的每一个都为
            //node节点
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
    void Logger::addAppender(LogAppender::ptr appender)
    {
        // 声明了友元所以可以直接访问
        MutexType::Lock lock(m_mutex);
        if (!appender->m_hasFormatter)
        { // 当appender未指定格式化器时
            // MutexType::Lock ll(appender->m_mutex);
            appender->m_formatter = m_formatter; // 则使用logger默认的
            // 但关键的是并没有修改appender的m_hasFormatter 即setformatter依旧会修改这个值
        }
        m_appenders.push_back(appender);
    }
    void Logger::addAppender(LogAppender::Type type, const std::string &filename)
    {
        LogAppender::ptr appender;
        if (type == LogAppender::Type::STDOUT)
        {
            appender.reset(new StdoutLogAppender);
        }
        else if (type == LogAppender::Type::FILE)
        {
            appender.reset(new FileLogAppender(filename));
        }
        if (appender)
        {
            addAppender(appender);
        }
    }

    void Logger::delAppender(LogAppender::ptr appender)
    {
        MutexType::Lock lock(m_mutex);
        m_appenders.remove(appender); // 删除所有地址与appendeer相同的元素
    }
    void Logger::clearAppenders()
    {
        MutexType::Lock lock(m_mutex);
        m_appenders.clear();
    }
    void Logger::setFormatter(LogFormatter::ptr val)
    {
        MutexType::Lock lock(m_mutex);
        m_formatter = val;
        for (auto &i : m_appenders)
        {
            if (!i->m_hasFormatter)
            {
                i->setFormatter(m_formatter);
            }
        }
    }
    void Logger::setFormatter(const std::string &val)
    {
        LogFormatter::ptr new_val(new LogFormatter(val));
        if (new_val->isError())
        {
            std::cout << "Logger setFormatter name=" << m_name
                      << " value=" << val << " invalid formatter"
                      << std::endl; // 我咋说formatter那里咋不报错 原来在这里 可以获取logger的名字 便于确定
        }
        setFormatter(new_val);//错误的依旧可以解析
    }
    void Logger::debug(LogEvent::ptr event)
    {
        log(LogLevel::DEBUG, event);
    }
    void Logger::info(LogEvent::ptr event)
    {
        log(LogLevel::INFO, event);
    }
    void Logger::warn(LogEvent::ptr event)
    {
        log(LogLevel::WARN, event);
    }
    void Logger::error(LogEvent::ptr event)
    {
        log(LogLevel::ERROR, event);
    }
    void Logger::fatal(LogEvent::ptr event)
    {
        log(LogLevel::FATAL, event);
    }
    LoggerManage::LoggerManage()
    {
        m_root.reset(new Logger("root"));
        m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
        // 默认添加一个控制台输出器
        m_loggers[m_root->getName()] = m_root;
    }
    // 获取日志器 如果没有则创建新的
    Logger::ptr LoggerManage::getLogger(const std::string &name)
    {
        MutexType::Lock lock(m_mutex);
        auto it = m_loggers.find(name);
        if (it != m_loggers.end())
        {
            return it->second;
        }
        Logger::ptr logger(new Logger(name));
        logger->m_root = m_root; // 设置主日志器 防止没有输出器时无法输出日志
        m_loggers[name] = logger;
        return logger;
    }
    struct LogAppenderDefine
    {
        int type = 0; // 1 File, 2 Stdout
        LogLevel::Level level = LogLevel::UNKNOW;
        std::string formatter;
        std::string file;
        bool operator==(const LogAppenderDefine &oth) const
        {
            return type == oth.type && level == oth.level && formatter == oth.formatter && file == oth.file;
        }
    };
    struct LogDefine
    {
        std::string name;
        LogLevel::Level level = LogLevel::UNKNOW;
        std::string formatter;
        std::vector<LogAppenderDefine> appenders;
        bool operator<(const LogDefine &o) const
        { // 为了在set里进行比较
            // 在set里判断是否相等 也是通过这个比较的
            return name < o.name;
        }
        bool operator==(const LogDefine &oth) const
        { // 为了比较两个配置是否相同
            return name == oth.name && level == oth.level && formatter == oth.formatter && appenders == appenders;
        }
    };
    // 读取string 转为对应的Log
    template <>
    class LexicalCast<std::string, struct LogDefine>
    {
    public:
        LogDefine operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            LogDefine ld;
            if (!node["name"].IsDefined()) // 判断名字
            {
                std::cout << "log config error: name is null, " << node
                          << std::endl;
                throw std::logic_error("log config name is null");
            }
            ld.name = node["name"].as<std::string>();
            // 读取level
            ld.level = LogLevel::FromString(node["level"].IsDefined() ? node["level"].as<std::string>() : "UNKNOW");
            // 读取formatter
            if (node["formatter"].IsDefined())
            {
                ld.formatter = node["formatter"].as<std::string>();
            }
            // 读取formatter
            if (node["appenders"].IsDefined())
            {
                for (size_t x = 0; x < node["appenders"].size(); ++x)
                {
                    YAML::Node a = node["appenders"][x];
                    if (!a["type"].IsDefined())
                    {
                        std::cout << "log config error: appender type is null, " << a << std::endl;
                        continue;
                    }
                    std::string type = a["type"].as<std::string>();
                    struct LogAppenderDefine lad;
                    // appender有两种类型 file和std
                    if (type == "FileLogAppender")
                    {
                        lad.type = 1;
                        if (!a["file"].IsDefined())
                        {
                            std::cout << "log config error: fileappender file is null, " << a
                                      << std::endl;
                            continue;
                        }
                        lad.file = a["file"].as<std::string>();
                        if (a["formatter"].IsDefined())
                        {
                            lad.formatter = a["formatter"].as<std::string>();
                        }
                    }
                    else if (type == "StdoutLogAppender")
                    {
                        lad.type = 2;
                        if (a["formatter"].IsDefined())
                        {
                            lad.formatter = a["formatter"].as<std::string>();
                        }
                    }
                    // 不是这两种
                    else
                    {
                        std::cout << "log config error: appender type is invalid, " << a
                                  << std::endl;
                        continue;
                    }
                    ld.appenders.push_back(lad);
                }
            }
            // std::cout<<ld.formatter <<" is successed"<<std::endl;
            return ld;
        }
    };
    // 将logDefine转为string
    template <>
    class LexicalCast<LogDefine, std::string>
    {
    public:
        std::string operator()(const LogDefine &i)
        {
            YAML::Node n;
            n["name"] = i.name;
            if (i.level != LogLevel::UNKNOW)
            {
                n["level"] = LogLevel::ToString(i.level);
            }
            if (!i.formatter.empty())
            {
                n["formatter"] = i.formatter;
            }
            for (auto &a : i.appenders)
            {
                YAML::Node na;
                if (a.type == 1)
                {
                    na["type"] = "FileLogAppender";
                    na["file"] = a.file;
                }
                else if (a.type == 2)
                {
                    na["type"] = "StdoutLogAppender";
                }
                if (a.level != LogLevel::UNKNOW)
                {
                    na["level"] = LogLevel::ToString(a.level);
                }

                if (!a.formatter.empty())
                {
                    na["formatter"] = a.formatter;
                }

                n["appenders"].push_back(na);
            }
            std::stringstream ss;
            ss << n;
            return ss.str();
        }
    };
    // 隐式调用Lookup //注册logs的配置
    ConfigVar<std::set<LogDefine>>::ptr g_log_defines = Config::Lookup("logs", std::set<LogDefine>(), "logs config");
    struct LogIniter
    {
        LogIniter()
        { // 添加监听器 当值修改时触发
            g_log_defines->addListener([](const std::set<LogDefine> &old_value, const std::set<LogDefine> &new_value)
                                       {
            FIBER_LOG_INFO(FIBER_LOG_ROOT())<<"on_logger_conf_changed";//日志配置变更时打印日志
            // std::cout<<new_value.size()<<std::endl;
            for(auto &i:new_value)
            {
                Logger::ptr logger;
                auto it=old_value.find(i);
                if(it==old_value.end()){//旧配置里没有这个姓名
                    logger=FIBER_LOG_NAME(i.name);
                }
                else{
                    if(!(i == *it)) {//有对应的name 但不完全相同
                        //修改的logger
                        logger = FIBER_LOG_NAME(i.name);
                    } 
                    else {//完全相同
                        continue;
                    }
                }
                logger->setLevel(i.level);
                if(!i.formatter.empty()) {//没指定 则为默认
                    // std::cout<<"not empty"<<std::endl;
                    logger->setFormatter(i.formatter);
                    // std::cout<<logger->getPattern()<<std::endl;
                }
                logger->clearAppenders();
                for(auto& a : i.appenders) {//读取LogAppenderDefine 进行配置
                    LogAppender::ptr ap;//给logappender对应值
                    if(a.type == 1) {//1表文件
                        ap.reset(new FileLogAppender(a.file));
                    } 
                    else if(a.type == 2){//2表示std
                        //  if(!FiberServer::EnvMgr::GetInstance()->has("d")) 后续升级 
                            ap.reset(new StdoutLogAppender());
                        // } else {
                        //     continue;
                        // }
                    }
                    ap->setLevel(a.level);
                    if(!a.formatter.empty()){//appender可以没有formatter 没有时会用logger默认的
                        LogFormatter::ptr fmt(new LogFormatter(a.formatter));
                        if(!fmt->isError()){
                            ap->setFormatter(fmt);
                        }
                        else{
                            std::cout<<"log,name="<<i.name<<" appender type="<<a.type<<" formatter="<<a.formatter<<" is invalid"<<std::endl;
                        }
                    }
                    logger->addAppender(ap);
                }
                // std::cout<<logger->getPattern()<<std::endl;
                // std::cout<<logger->toYamlString()<<std::endl;
            }
            //删除在新配置不存在的配置 
             for(auto& i : old_value) {
                auto it = new_value.find(i);
                if(it == new_value.end()) {
                    //软删除logger 不直接删除防止有些程序还在使用 这样访问删除的内存
                    // 如果还用使用这个则会调用root的输出器
                    auto logger = FIBER_LOG_NAME(i.name);
                    logger->setLevel((LogLevel::Level::UNKNOW));
                    logger->clearAppenders();
                }
            }
        } // 第一个总for函数结束
      );//匿名函数的 ）和句子结尾的;
     }//LogIniter的右大括号
    };//struct的右括号
    static LogIniter __log_init; // 执行初始化函数
 // 说明下log的配置总过程 首先lookup在config里注册logs 然后在logIniter构造函数为logs注册回调函数
 //  接着在main程序里手动调用LoadFromConfDir里面调用LoadFromYaml调用logs.formString()
}
