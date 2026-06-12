#include "util.h"
#include<pthread.h>
#include<cstring>
#include<unistd.h>
#include<dirent.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<signal.h>
#include<chrono>
#include <stdarg.h>
#include<sys/syscall.h>
#include<sstream>
#include <execinfo.h>
#include <filesystem>
#include"log.h"
namespace FiberServer{
    static thread_local pid_t t_thread_id = 0;
    pid_t GetThreadId(){
        if(t_thread_id==0){
            t_thread_id=syscall(SYS_gettid);
        }
        return t_thread_id;
    }
    uint64_t GetCurrentMS(){
        using namespace std::chrono;
        auto now=system_clock::now();
        auto ms=duration_cast<milliseconds>(now.time_since_epoch());
        return ms.count();
    }
    //时间转字符串
    std::string Time2Str(time_t ts,const std::string &format){
        struct tm tm;
        localtime_r(&ts,&tm);
        char buf[64];
        strftime(buf,sizeof(buf),format.c_str(),&tm);
        return buf;
    }
    //字符串转时间
    time_t Str2Time(const std::string &str,const std::string &format){
        struct tm tm;
        memset(&tm,0,sizeof(tm));
        strptime(str.c_str(),format.c_str(),&tm);
        return mktime(&tm);
    }
    time_t Str2Time(const char* str, const char* format) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    if(!strptime(str, format, &t)) {
        return 0;
    }
    return mktime(&t);
    }

    static std::string demangle(const char* str) {
    size_t size = 0;
    int status = 0;
    std::string rt;
    rt.resize(256);
    if(1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
        char* v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if(v) {
            std::string result(v);
            free(v);
            return result;
        }
    }
    if(1 == sscanf(str, "%255s", &rt[0])) {
        return rt;
    }
    return str;
    }
    void Backtrace(std::vector<std::string>&bt,int size,int skip){
        void** array= (void**)malloc((sizeof(void*))*size);
        size_t s=::backtrace(array,size);
        char** strings=::backtrace_symbols(array,s);
        if(strings==nullptr){
            FIBER_LOG_ERROR(FIBER_LOG_ROOT())<<"backtrace_symbols error"<<std::endl;;
            free(array);
            return;
        }
        for(size_t i=skip;i<s;++i){
             bt.push_back(demangle(strings[i]));
        }
        free(strings);
        free(array);
        return;
    }
    std::string BacktraceToString(int size,int skip,const std::string& prefix){
        std::vector<std::string> bt;
        Backtrace(bt,size,skip);
        std::stringstream ss;
        for(size_t i=skip;i<bt.size();++i){
            ss<<prefix<<bt[i]<<std::endl;
        }
        return ss.str();
    }
    
bool YamlToJson(const YAML::Node& ynode, Json::Value& jnode) {
    try {
        if(ynode.IsScalar()) {
            Json::Value v(ynode.Scalar());
            jnode.swapPayload(v);
            return true;
        }
        if(ynode.IsSequence()) {
            for(size_t i = 0; i < ynode.size(); ++i) {
                Json::Value v;
                if(YamlToJson(ynode[i], v)) {
                    jnode.append(v);
                } else {
                    return false;
                }
            }
        } else if(ynode.IsMap()) {
            for(auto it = ynode.begin();
                    it != ynode.end(); ++it) {
                Json::Value v;
                if(YamlToJson(it->second, v)) {
                    jnode[it->first.Scalar()] = v;
                } else {
                    return false;
                }
            }
        }
    } catch(...) {
        return false;
    }
    return true;
}

bool JsonToYaml(const Json::Value& jnode, YAML::Node& ynode) {
    try {
        if(jnode.isArray()) {
            for(int i = 0; i < (int)jnode.size(); ++i) {
                YAML::Node n;
                if(JsonToYaml(jnode[i], n)) {
                    ynode.push_back(n);
                } else {
                    return false;
                }
            }
        } else if(jnode.isObject()) {
            for(auto it = jnode.begin();
                    it != jnode.end();
                    ++it) {
                YAML::Node n;
                if(JsonToYaml(*it, n)) {
                    ynode[it.name()] = n;
                } else {
                    return false;
                }
            }
        } else {
            ynode = jnode.asString();
        }
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<int> FSUtil::GetIndices(const std::string& path) {
    std::vector<int> indices;
    
    // 1. 打开目录
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        // 如果目录不存在，说明还没开始上传，直接返回空列表
        return indices;
    }

    struct dirent* entry;
    // 2. 遍历目录下的所有文件
    while ((entry = readdir(dir)) != nullptr) {
        // 只处理普通文件 (DT_REG)，忽略目录 (.) 和 (..)
        if (entry->d_type == DT_REG) {
            try {
                // 将文件名（如 "1"）转换为整数
                int index = std::stoi(entry->d_name);
                indices.push_back(index);
            } catch (...) {
                // 如果文件名不是数字（比如隐藏文件或临时文件），直接跳过
                continue;
            }
        }
    }
    closedir(dir);

    // 3. 关键步骤：对编号进行升序排序 (0, 1, 2, 3...)
    std::sort(indices.begin(), indices.end());

    return indices;
}

void FSUtil::ListAllFile(std::vector<std::string>&files,
    const std::string& path,const std::string& subfix){
    if(access(path.c_str(), 0) != 0) {
        return;
    }
    DIR* dir = opendir(path.c_str());
    if(dir == nullptr) {
        return;
    }
    struct dirent* dp = nullptr;
    while((dp = readdir(dir)) != nullptr) {
        if(dp->d_type == DT_DIR) {
            if(!strcmp(dp->d_name, ".")
                || !strcmp(dp->d_name, "..")) {
                continue;
            }
            ListAllFile(files, path + "/" + dp->d_name, subfix);
        } else if(dp->d_type == DT_REG) {
            std::string filename(dp->d_name);
            if(subfix.empty()) {
                files.push_back(path + "/" + filename);
            } else {
                if(filename.size() < subfix.size()) {
                    continue;
                }
                if(filename.substr(filename.length() - subfix.size()) == subfix) {
                    files.push_back(path + "/" + filename);
                }
            }
        }
    }
    closedir(dir);
}

void FSUtil::ListAllDir(std::vector<std::string>& dirs,
    const std::string& path, const std::string& subfix) {
    if (access(path.c_str(), 0) != 0) {
        return;
    }
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }
    struct dirent* dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        if (dp->d_type == DT_DIR) {
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
                continue;
            }
            std::string fullPath = path + "/" + dp->d_name;
            if (subfix.empty()) {
                dirs.push_back(fullPath);
            } else {
                std::string dirname(dp->d_name);
                if (dirname.size() >= subfix.size() &&
                    dirname.substr(dirname.length() - subfix.size()) == subfix) {
                    dirs.push_back(fullPath);
                }
            }
            ListAllDir(dirs, fullPath, subfix);
        }
    }
    closedir(dir);
}

bool FSUtil::IsDirEmpty(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    struct dirent* dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            closedir(dir);
            return false;
        }
    }
    closedir(dir);
    return true;
}

bool FSUtil::RmEmptyDir(const std::string& path) {
    // rmdir 系统调用非常安全：如果目录里还有文件，它会直接报错返回 -1
    // 我们不需要写复杂的逻辑，直接调它即可
    return rmdir(path.c_str()) == 0;
}
static int __lstat(const char* file, struct stat* st = nullptr) {
    struct stat lst;
    int ret = lstat(file, &lst);
    if(st) {
        *st = lst;
    }
    return ret;
    }
static int __mkdir(const char* dirname) {
    if(access(dirname, F_OK) == 0) {
        return 0;
    }
    return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }

    // --- 函数 1：核心物理合并 (处理全路径字符串) ---
bool FSUtil::MergeFiles(const std::string& target_path, 
    const std::vector<std::string>& file_paths) {
// 1. 以二进制写入模式打开目标文件 (不使用 append，直接创建/覆盖)
std::ofstream ofs(target_path, std::ios::binary);
if (!ofs.is_open()) {
return false;
}

// 2. 设置 1MB 缓冲区，平衡内存占用与磁盘 IO 性能
const size_t buffer_size = 1024 * 1024;
std::vector<char> buffer(buffer_size);

for (const auto& src_path : file_paths) {
std::ifstream ifs(src_path, std::ios::binary);
if (!ifs.is_open()) {
return false; // 某个分片打不开，合并即失败
}

// 3. 循环读取并写入目标文件
while (ifs) {
ifs.read(buffer.data(), buffer_size);
std::streamsize bytes_read = ifs.gcount();
if (bytes_read > 0) {
ofs.write(buffer.data(), bytes_read);
}
}
ifs.close();
}

ofs.close();
return true;
}
uint64_t FSUtil::DirTime(const std::string& path){
    struct stat st;
    if(lstat(path.c_str(), &st)) {
        return 0;
    }
    return st.st_mtime;
}

// --- 函数 2：业务转换合并 (处理数字编号) ---
bool FSUtil::MergeFiles(const std::string& target_path, 
    const std::string& src_dir, 
    const std::vector<int>& indices) {
std::vector<std::string> full_paths;

// 1. 将 0, 1, 2... 转换为物理路径 "/path/to/chunk/0"
for (int idx : indices) {
// 注意：这里路径拼接要确保 src_dir 末尾是否有斜杠，保险起见手动加一个
std::string path = src_dir;
if (path.back() != '/') path += "/";
full_paths.push_back(path + std::to_string(idx));
}

// 2. 调用上面的核心物理合并函数
return MergeFiles(target_path, full_paths);
}

bool FSUtil::Mkdir(const std::string& dirname) {
    if(__lstat(dirname.c_str()) == 0) {
        return true;
    }
    char* path = strdup(dirname.c_str());
    char* ptr = strchr(path + 1, '/');
    do {
        for(; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) {
            *ptr = '\0';
            if(__mkdir(path) != 0) {
                break;
            }
        }
        if(ptr != nullptr) {
            break;
        } else if(__mkdir(path) != 0) {
            break;
        }
        free(path);
        return true;
    } while(0);
    free(path);
    return false;
}

    bool FSUtil::IsRunningPidfile(const std::string& pidfile) {
    if(__lstat(pidfile.c_str()) != 0) {
        return false;
    }
    std::ifstream ifs(pidfile);
    std::string line;
    if(!ifs || !std::getline(ifs, line)) {
        return false;
    }
    if(line.empty()) {
        return false;
    }
    pid_t pid = atoi(line.c_str());
    if(pid <= 1) {
        return false;
    }
    if(kill(pid, 0) != 0) {
        return false;
    }
    return true;
}

    bool FSUtil::Unlink(const std::string& filename, bool exist) {
    if(!exist && __lstat(filename.c_str())) {
        return true;
    }
    return ::unlink(filename.c_str()) == 0;
}

bool FSUtil::IsDir(const std::string& path) {
    struct stat st;
    if(lstat(path.c_str(), &st)) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

uint64_t FSUtil::GetFileTime(const std::string& path) {
    struct stat st;
    if(lstat(path.c_str(), &st)) {
        return 0;
    }
    return st.st_mtime;
}
    
bool FSUtil::Rm(const std::string& path) {
    struct stat st;
    if(lstat(path.c_str(), &st)) {
        return true;
    }
    if(!(st.st_mode & S_IFDIR)) {
        return Unlink(path);
    }

    DIR* dir = opendir(path.c_str());
    if(!dir) {
        return false;
    }
    
    bool ret = true;
    struct dirent* dp = nullptr;
    while((dp = readdir(dir))) {
        if(!strcmp(dp->d_name, ".")
                || !strcmp(dp->d_name, "..")) {
            continue;
        }
        std::string dirname = path + "/" + dp->d_name;
        ret = Rm(dirname);
    }
    closedir(dir);
    if(::rmdir(path.c_str())) {
        ret = false;
    }
    return ret;
}

bool FSUtil::Mv(const std::string& from, const std::string& to) {
    if(!Rm(to)) {
        return false;
    }
    // if(Mkdir(to))
        return rename(from.c_str(), to.c_str()) == 0;
    return false;
}

bool FSUtil::Realpath(const std::string& path, std::string& rpath) {
    if(__lstat(path.c_str())) {
        return false;
    }
    char* ptr = ::realpath(path.c_str(), nullptr);
    if(nullptr == ptr) {
        return false;
    }
    std::string(ptr).swap(rpath);
    free(ptr);
    return true;
}

bool FSUtil::Symlink(const std::string& from, const std::string& to) {
    if(!Rm(to)) {
        return false;
    }
    return ::symlink(from.c_str(), to.c_str()) == 0;
}

std::string FSUtil::Dirname(const std::string& filename) {
    if(filename.empty()) {
        return ".";
    }
    auto pos = filename.rfind('/');
    if(pos == 0) {
        return "/";
    } else if(pos == std::string::npos) {
        return ".";
    } else {
        return filename.substr(0, pos);
    }
}

    std::string FSUtil::Basename(const std::string& filename) {
    if(filename.empty()) {
        return filename;
    }
    auto pos = filename.rfind('/');
    if(pos == std::string::npos) {
        return filename;
    } else {
        return filename.substr(pos + 1);
    }
}

    bool FSUtil::OpenForRead(std::ifstream& ifs, const std::string& filename
                        ,std::ios_base::openmode mode) {
    ifs.open(filename.c_str(), mode);
    return ifs.is_open();
}

    bool FSUtil::OpenForWrite(std::ofstream& ofs, const std::string& filename
                        ,std::ios_base::openmode mode) {
    ofs.open(filename.c_str(), mode);   
    if(!ofs.is_open()) {
        std::string dir = Dirname(filename);
        Mkdir(dir);
        ofs.open(filename.c_str(), mode);
    }
    return ofs.is_open();
}

std::string StringUtil::Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto v = Formatv(fmt, ap);
    va_end(ap);
    return v;
}

std::string StringUtil::Formatv(const char* fmt, va_list ap) {
    char* buf = nullptr;
    auto len = vasprintf(&buf, fmt, ap);
    if(len == -1) {
        return "";
    }
    std::string ret(buf, len);
    free(buf);
    return ret;
}

static const char uri_chars[256] = {
    /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 0, 0,
    /* 64 */
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    /* 128 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static const char xdigit_chars[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define CHAR_IS_UNRESERVED(c)           \
    (uri_chars[(unsigned char)(c)])

//-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~
std::string StringUtil::UrlEncode(const std::string& str, bool space_as_plus) {
    static const char *hexdigits = "0123456789ABCDEF";
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str() ; c < end; ++c) {
        if(!CHAR_IS_UNRESERVED(*c)) {
            if(!ss) {
                ss = new std::string;
                ss->reserve(str.size() * 1.2);
                ss->append(str.c_str(), c - str.c_str());
            }
            if(*c == ' ' && space_as_plus) {
                ss->append(1, '+');
            } else {
                ss->append(1, '%');
                ss->append(1, hexdigits[(uint8_t)*c >> 4]);
                ss->append(1, hexdigits[*c & 0xf]);
            }
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::UrlDecode(const std::string& str, bool space_as_plus) {
    std::string* ss = nullptr;
    const char* end = str.c_str() + str.length();
    for(const char* c = str.c_str(); c < end; ++c) {
        if(*c == '+' && space_as_plus) {
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, ' ');
        } else if(*c == '%' && (c + 2) < end
                    && isxdigit(*(c + 1)) && isxdigit(*(c + 2))){
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
            c += 2;
        } else if(ss) {
            ss->append(1, *c);
        }
    }
    if(!ss) {
        return str;
    } else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

std::string StringUtil::Trim(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) {
        return "";
    }
    auto end = str.find_last_not_of(delimit);
    return str.substr(begin, end - begin + 1);
}

std::string StringUtil::TrimLeft(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) {
        return "";
    }
    return str.substr(begin);
}

std::string StringUtil::TrimRight(const std::string& str, const std::string& delimit) {
    auto end = str.find_last_not_of(delimit);
    if(end == std::string::npos) {
        return "";
    }
    return str.substr(0, end);
}

std::string StringUtil::WStringToString(const std::wstring& ws) {
    std::string str_locale = setlocale(LC_ALL, "");
    const wchar_t* wch_src = ws.c_str();
    size_t n_dest_size = wcstombs(NULL, wch_src, 0) + 1;
    char *ch_dest = new char[n_dest_size];
    memset(ch_dest,0,n_dest_size);
    wcstombs(ch_dest,wch_src,n_dest_size);
    std::string str_result = ch_dest;
    delete []ch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return str_result;
}

std::wstring StringUtil::StringToWString(const std::string& s) {
    std::string str_locale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t n_dest_size = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wch_dest = new wchar_t[n_dest_size];
    wmemset(wch_dest, 0, n_dest_size);
    mbstowcs(wch_dest,chSrc,n_dest_size);
    std::wstring wstr_result = wch_dest;
    delete []wch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return wstr_result;
}
int8_t  TypeUtil::ToChar(const std::string& str) {
    if(str.empty()) {
        return 0;
    }
    return *str.begin();
}

int64_t TypeUtil::Atoi(const std::string& str) {
    if(str.empty()) {
        return 0;
    }
    return strtoull(str.c_str(), nullptr, 10);
}

double  TypeUtil::Atof(const std::string& str) {
    if(str.empty()) {
        return 0;
    }
    return atof(str.c_str());
}

int8_t  TypeUtil::ToChar(const char* str) {
    if(str == nullptr) {
        return 0;
    }
    return str[0];
}

int64_t TypeUtil::Atoi(const char* str) {
    if(str == nullptr) {
        return 0;
    }
    return strtoull(str, nullptr, 10);//将字符串转为ull
}

double  TypeUtil::Atof(const char* str) {
    if(str == nullptr) {
        return 0;
    }
    return atof(str);
}

}