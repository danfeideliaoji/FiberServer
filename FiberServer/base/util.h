#pragma once
#include<pthread.h>
#include<stdint.h>
#include<time.h>
#include<iostream>
#include<cxxabi.h>
#include<string>
#include<vector>
#include <yaml-cpp/yaml.h>
#include <boost/lexical_cast.hpp>
#include "FiberServer/util/json_util.h"
#include "FiberServer/util/hash_util.h"
namespace FiberServer{
    pid_t  GetThreadId();//获得线程id
    uint64_t GetCurrentMS();//获得当前毫秒
    std::string Time2Str(time_t ts,const std::string &format="%Y-%m-%d %H:%M:%S");//将时间转为字符串
    time_t Str2Time(const std::string &str,const std::string &format="%Y-%m-%d %H:%M:%S");//将字符串转为时间
    time_t Str2Time(const char* str, const char* format = "%Y-%m-%d %H:%M:%S");
    template<typename T>
    const char* TypeToName(){//获得类型名
    // abi::__cxa_demangle 是 Linux/Unix 下 GCC/Clang 提供的系统函数
    // 专门用来将混淆后的名字 (mangled name) 还原成人类可读的名字 (demangled name)
    static const char* s_name = abi::__cxa_demangle(
        //使用静态 相当于本地缓存 提高性能
        typeid(T).name(),   // 1. 传入混淆的类型名
        nullptr,            // 2. 输出缓冲区 (nullptr 表示让函数自己 malloc 分配)
        nullptr,            // 3. 长度 (nullptr 表示不关心)
        nullptr             // 4. 状态 (nullptr 表示不关心成功与否)
    );
    return s_name;
    }
    void Backtrace(std::vector<std::string>&bt,int size,int skip);
    bool YamlToJson(const YAML::Node& ynode, Json::Value& jnode);
    bool JsonToYaml(const Json::Value& jnode, YAML::Node& ynode);
    std::string BacktraceToString(int size=64,int skip=0,const std::string& prefix="");
    class FSUtil{
    public:

        static std::vector<int> GetIndices(const std::string& path);
        //遍历并获取指定目录下所有符合条件的文件列表。
        static void ListAllFile(std::vector<std::string>& files,
        const std::string &path,const std::string& subfix);
        
        //遍历并获取指定目录下所有子目录列表
        static void ListAllDir(std::vector<std::string>& dirs,
        const std::string &path,const std::string& subfix);
        
        //检查目录是否为空
        static bool IsDirEmpty(const std::string& path);
        
        //创建一个新目录
        static bool Mkdir(const std::string& dirname);
        
        //通过 PID 文件检查某个程序是否正在运行
        static bool IsRunningPidfile(const std::string& pidfile);
        
        //删除指定路径的文件
        static bool Rm(const std::string& path);


        static bool MergeFiles(const std::string& target_path,const std::vector<std::string>& file_paths);
        static bool MergeFiles(const std::string& target_path,const std::string& src_dir,const std::vector<int>& indices);


        //移动文件
        static bool Mv(const std::string& from,const std::string& to);
        
        //获取文件的绝对真实路径
        static bool Realpath(const std::string&path,std::string& rpath);
        
        //创建一个符号链接（软链接)
        static bool Symlink(const std::string& frm,const std::string& to);
        
        //删除文件系统中的一个名称(解除软链接)
        static bool Unlink(const std::string& filename, bool exist = false);

        //检查路径是否为目录
        static bool IsDir(const std::string& path);

        //获取文件修改时间（毫秒级时间戳）
        static uint64_t GetFileTime(const std::string& path);

        //删除空目录
        static bool RmEmptyDir(const std::string& path);

        uint64_t DirTime(const std::string& path);

        //提取文件路径中的目录部分
        static std::string Dirname(const std::string& filename);
        
        //提取文件路径中的纯文件名部分(最后的那个文件名)
        static std::string Basename(const std::string& filename);
        
        //以指定的模式打开一个文件用于读取。
        static bool OpenForRead(std::ifstream& ifs, const std::string& filename
                    ,std::ios_base::openmode mode);
        
        //以指定的模式打开一个文件用于写入
        static bool OpenForWrite(std::ofstream& ofs, const std::string& filename
                    ,std::ios_base::openmode mode); 
    };
class StringUtil {
public:
    static std::string Format(const char* fmt, ...);
    static std::string Formatv(const char* fmt, va_list ap);

    static std::string UrlEncode(const std::string& str, bool space_as_plus = true);
    static std::string UrlDecode(const std::string& str, bool space_as_plus = true);

    static std::string Trim(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimLeft(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimRight(const std::string& str, const std::string& delimit = " \t\r\n");


    static std::string WStringToString(const std::wstring& ws);
    static std::wstring StringToWString(const std::string& s);

};

class TypeUtil {
    public:
        static int8_t ToChar(const std::string& str);
        static int64_t Atoi(const std::string& str);
        static double Atof(const std::string& str);
        static int8_t ToChar(const char* str);
        static int64_t Atoi(const char* str);
        static double Atof(const char* str);
    };

template<class V, class Map, class K>
V GetParamValue(const Map& m, const K& k, const V& def = V()) {
    auto it = m.find(k);
    if(it == m.end()) {
        return def;
    }
    try {
        return boost::lexical_cast<V>(it->second);
    } catch (...) {
    }
    return def;
}

template<class V, class Map, class K>
bool CheckGetParamValue(const Map& m, const K& k, V& v) {
    auto it = m.find(k);
    if(it == m.end()) {
        return false;
    }
    try {
        v = boost::lexical_cast<V>(it->second);
        return true;
    } catch (...) {
    }
    return false;
}
}
