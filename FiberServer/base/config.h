#pragma once
#include <boost/lexical_cast.hpp>
#include <memory>
#include <string>
#include <cctype>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <functional>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <yaml-cpp/yaml.h>
#include "log.h" //log config这俩互相包含对方 config在头文件里引用 log在源文件引用 解决了互相引用的问题
#include "util.h"
#include "mutex.h"
namespace FiberServer
{
    // 这里的ConfigVarBase ConfigVar 设计的非常非常巧妙 我查了下叫做类型擦除
    class ConfigVarBase
    { // 配置变量的基类
    public:
        typedef std::shared_ptr<ConfigVarBase> ptr;
        ConfigVarBase(const std::string &name, const std::string &description = "") : m_name(name), m_description(description)
        {
            std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower); // 名字用小写
        }
        virtual ~ConfigVarBase(){};
        virtual std::string toString() = 0;
        virtual bool fromString(const std::string &val) = 0;
        virtual std::string getTypeName() const = 0;//这里纯虚函数是为了多态时使用子类的这个函数
        const std::string &getName() const { return m_name; }
        const std::string &getDescription() const { return m_description; }

    protected:
        std::string m_name;        // 该配置的名字
        std::string m_description; // 该配置的描述
    };
    // 类型转换模版类(F 源类型, T 目标类型)
    template <class F, class T>
    class LexicalCast
    { // 转换类 负责进行各种转换
    public:
        T operator()(const F &v)
        {                                     // 仿函数
            return boost::lexical_cast<T>(v); // 用boost的lexical_cast转换
        }
    };
    // String 转换成 std::vector<T>
    template <typename T>
    class LexicalCast<std::string, std::vector<T>>
    {
    public:
        std::vector<T> operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            typename std::vector<T> vec;// 前面的typename不能省略
            std::stringstream ss;
            for (size_t i = 0; i < node.size(); i++)
            {
                ss.str("");
                ss << node[i];
                vec.puch_back(LexicalCast<std::string, T>()(ss.str()));
            }
            return vec;
        }
    };
    // std::vector<T> 转换成 YAML String
    template <typename T>
    class LexicalCast<std::vector<T>, std::string>
    {
    public:
        std::string operator()(const std::vector<T> &v)
        {
            YAML::Node node(YAML::NodeType::Sequence);//建立node序列
            for (auto &i : v)
            {
                node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
            }
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
    // 类型转换模板类片特化(YAML String 转换成 std::list<T>
    template <class T>
    class LexicalCast<std::string, std::list<T>>
    {
    public:
        std::list<T> operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            typename std::list<T> vec;
            std::stringstream ss;
            for (size_t i = 0; i < node.size(); ++i)
            {
                ss.str("");
                ss << node[i];
                vec.push_back(LexicalCast<std::string, T>()(ss.str()));
            }
            return vec;
        }
    };
    // list转为string
    template <class T>
    class LexicalCast<std::list<T>, std::string>
    {
    public:
        std::string operator()(const std::list<T> &v)
        {
            YAML::Node node(YAML::NodeType::Sequence);
            for (auto &i : v)
            {
                node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
            }
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
 
    // 将stirng转为set
    template <typename T>
    class LexicalCast<std::string, std::set<T>>
    {
    public:
        std::set<T> operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            typename std::set<T> vec;
            std::stringstream ss;
            for (size_t i = 0; i < node.size(); ++i)
            {
                ss.str("");
                ss << node[i];
                vec.insert(LexicalCast<std::string, T>()(ss.str()));
            }
            return vec;
        }
    };
    // 将set转为stirng
    template <class T>
    class LexicalCast<std::set<T>, std::string>
    {
    public:
        std::string operator()(const std::set<T> &v)
        {
            YAML::Node node(YAML::NodeType::Sequence);
            for (auto &i : v)
            {
                node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
            }
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
    // 将string转为map<stirng,T>
    template <class T>
    class LexicalCast<std::string, std::map<std::string, T>>
    {
    public:
        std::map<std::string, T> operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            typename std::map<std::string, T> vec;
            std::stringstream ss;
            for (auto it = node.begin();
                 it != node.end(); ++it)
            {
                ss.str("");
                ss << it->second;
                vec.insert(std::make_pair(it->first.Scalar(),
                                          LexicalCast<std::string, T>()(ss.str())));
            }
            return vec;
        }
    };
    // 将map<std::string,T>转为string
    template <class T>
    class LexicalCast<std::map<std::string, T>, std::string>
    {
    public:
        std::string operator()(const std::map<std::string, T> &v)
        {
            YAML::Node node(YAML::NodeType::Map);
            for (auto &i : v)
            {
                node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
            }
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
    // 将string转为unordered_map<std::string, T>
    template <class T>
    class LexicalCast<std::string, std::unordered_map<std::string, T>>
    {
    public:
        std::unordered_map<std::string, T> operator()(const std::string &v)
        {
            YAML::Node node = YAML::Load(v);
            typename std::unordered_map<std::string, T> vec;
            std::stringstream ss;
            for (auto it = node.begin();
                 it != node.end(); ++it)
            {
                ss.str("");
                ss << it->second;
                vec.insert(std::make_pair(it->first.Scalar(),
                                          LexicalCast<std::string, T>()(ss.str())));
            }
            return vec;
        }
    };
    // 将unordered_map<std::string, T>转为string
    template <class T>
    class LexicalCast<std::unordered_map<std::string, T>, std::string>
    {
    public:
        std::string operator()(const std::unordered_map<std::string, T> &v)
        {
            YAML::Node node(YAML::NodeType::Map);
            for (auto &i : v)
            {
                node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
            }
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
    // 大佬的思路太太牛逼了
    template <class T, class FromStr = LexicalCast<std::string, T>,
              class ToStr = LexicalCast<T, std::string>> // 两个转换类
    class ConfigVar : public ConfigVarBase
    {
    public:
        typedef std::shared_ptr<ConfigVar> ptr;
        typedef RWMutex RWMutexType;
        typedef std::function <void(const T &old_value, const T &new_value)> on_change_cb;
        ConfigVar(const std::string &name, const T &value, const std::string &description = "") : ConfigVarBase(name, description), m_val(value) {}
        std::string toString() override
        { // 将配置格式转为YAML的string格式
            try
            {
                RWMutexType::ReadLock lock(m_mutex); // 用读锁 因为这里不会修改
                return ToStr()(m_val);               // ToStr是模板类 第一个()是创建对象 第二个是调用
            }
            catch (std::exception &e)
            {
                FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "ConfigVar::toString exception "
                                              << e.what() << " convert: " << TypeToName<T>() << " to string"
                                              << " name=" << m_name; // TypeToName在util里
            }
            return "";
        }
        bool fromString(const std::string &val) override
        { // 读取YAML的string
            try
            {
                setValue(FromStr()(val));
            }
            catch (std::exception &e)
            {
                FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "ConfigVar::fromString exception "
                                              << e.what() << " convert: string to " << TypeToName<T>()
                                              << " name=" << m_name
                                              << " val: " << val;
                return false;
            }
            return true;
        }
        const T &getValue()
        {
            RWMutexType::ReadLock lock(m_mutex);
            return m_val;
        }
        void setValue(const T &v)
        { // 赋值时 会触发回调函数
            {
                RWMutexType::ReadLock lock(m_mutex);
                if (v == m_val)
                {
                    return;
                }
                for (auto &i : m_cbs)
                {
                    i.second(m_val, v);
                }
            }
            {
                RWMutexType::WriteLock lock(m_mutex);
                m_val = v;
            }
        }
        std::string getTypeName() const override
        {                           // 获得该类型的字符串
            return TypeToName<T>(); // util里的获得类型
        }
        // 添加回调函数
        uint64_t addListener(on_change_cb cb)
        {
            static uint64_t s_fun_id = 0;
            RWMutexType::WriteLock lock(m_mutex);
            ++s_fun_id;
            m_cbs[s_fun_id] = cb;
            return s_fun_id;
        }
        // 删除对应的回调函数
        void delListener(uint64_t key)
        {
            RWMutexType::WriteLock lock(m_mutex);
            m_cbs.erase(key);
        }
        // 获取回调函数 如果存在返回对应的回调函数,否则返回nullptr
        on_change_cb getListener(uint64_t key)
        {
            RWMutexType::ReadLock lock(m_mutex);
            auto it = m_cbs.find(key);
            return it == m_cbs.end() ? nullptr : it->second;
        }
        // 清理所有回调函数
        void clearListener()
        {
            RWMutexType::WriteLock lock(m_mutex);
            m_cbs.clear();
        }

    private:
        T m_val;
        RWMutexType m_mutex;
        std::map<uint64_t, on_change_cb> m_cbs; // 注册的回调函数 可以注册多个
    };
class Config// 管理类
{
    public:
        typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap; // 写到这里我才明白为啥要写一个基类 基础类
        // 我还以为是为了拓展 没想到是利用多态 这样就可以操作不同的模版类 大佬太强了!!!!!!
        typedef RWMutex RWMutexType;
        // 使用静态的 原因是先初始化防止用时还没有 因为如果创建全局变量的话 顺序不确定 用静态防止其他全局变量用时表还没创建
        static ConfigVarMap &GetDatas()
        {
            static ConfigVarMap s_datas;
            return s_datas;
        }
        // 查找如果存在的话则返回 如果不存在的话则创建
        template <class T>
        static typename ConfigVar<T>::ptr Lookup(
            const std::string &name,
            const T &default_value,
            const std::string &descripton = "")
        {
            auto map = GetDatas(); // 获得静态初始化表
            {
                RWMutexType::ReadLock lock(GetMutex());
                auto it = map.find(name);
                if (it != map.end())
                { // 找到
                    auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);//右边明确类型 用auto就行
                    if (tmp)
                    { // 转换成功
                        FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "Lookup name=" << name << " exists";
                        return tmp;
                    }
                    else
                    { // 转换失败
                        FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                                                    << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                                                    << " " << it->second->toString();
                        return nullptr;
                    }
                }
            }
            if (name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos)
            { // 判断命名是否符合规则
                FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "Lookup name invalid " << name;
                throw std::invalid_argument(name);
            }
            typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value,descripton));
            RWMutexType::WriteLock(getMutex());
            GetDatas()[name] = v;
            return v;
        }
        // 返回配置参数名为name的配置参数
        template <class T>
        static typename ConfigVar<T>::ptr Lookup(const std::string &name)
        {
           
            RWMutexType::ReadLock lock(GetMutex());
            auto it = GetDatas().find(name);
            if (it == GetDatas().end())
            { // 找不到 返回空
                return nullptr;
            }
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
            if (!tmp)
            { // 如果类型不正确的话
                FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                                              << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                                              << " " << it->second->toString();
                return nullptr;
            }
            return tmp;
        }
        /*
        其实我想法是这样的 因为我觉得我lookup都要对应的配置了 还需要我指定类型 有点不太好
        然而似乎只能那样做 下面的有几个天大的bug cpp没有反射机制 所以没法动态获得类型
        static auto Lookup(const std::string& name) {
            auto it = GetDatas().find(name);
            if(it == GetDatas().end()) {//找不到 返回空
                return nullptr;
            }
            auto tmp=std::dynamic_pointer_cast<ConfigVar<it->second->getTypeName()>> (it->second);
            return tmp;
        }
         */

        // 获得对应的基类
        static ConfigVarBase::ptr LookupBase(const std::string &name);
        static void LoadFromYaml(const YAML::Node &root);                         // 使用YAML::Node初始化配置模块
        static void LoadFromConfDir(const std::string &path, bool force = false); // 加载path文件夹里面的配置文件
        static void Visit(std::function<void(ConfigVarBase::ptr)> cb);
    private:
        static RWMutexType& GetMutex() {
            static RWMutexType s_mutex;
            return s_mutex;
        }
        // RWMutexType m_mutex; 使用的函数为静态函数 所以不能直接创建 在静态函数里创建静态变量
    };
}
