#include<yaml-cpp/yaml.h>
#include<list>
#include<cctype>
#include "config.h"
namespace FiberServer{
static Logger::ptr g_logger=FIBER_LOG_NAME("system");
//查找获得对应的基类
ConfigVarBase::ptr Config::LookupBase(const std::string& name) {//写的时候没觉得啥
  // 要用的时候才发现牛逼 正常查找lookup要提供你要的类型 而这个是不用提供类型的
RWMutexType::ReadLock(GetMutex());
auto it = GetDatas().find(name);
return it == GetDatas().end() ? nullptr : it->second; //发生多态
}

/*
A:
  B:1
  C:2
*/
//递归遍历Node 又是大佬的操作
static void ListAllMember(const std::string& prefix,
const YAML::Node& node,std::list<std::pair<std::string,const YAML::Node>>& output)
{
    //如果命名不合规范
    if(prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")!=std::string::npos){
        FIBER_LOG_ERROR(FIBER_LOG_ROOT())<<"Config invalid name: "<<prefix<<" : "<<node;
        return;
    }
    output.push_back(std::make_pair(prefix,node ));
    if(node.IsMap()){//如果是map 比如就上面的A
        for(auto it=node.begin();it!=node.end();++it){ //大佬都是++在前面
            ListAllMember(prefix.empty()?it->first.Scalar():prefix+"."+it->first.Scalar()
             ,it->second,output);//给子代prefix赋值
        }
    }
}
//读取Node到对应的config map里面
void Config::LoadFromYaml(const YAML::Node &root){
    std::list<std::pair<std::string, const YAML::Node> > all_nodes;
    ListAllMember("",root,all_nodes);
    for(auto &i:all_nodes){
        std::string key=i.first;
        if(key.empty()){
            continue;
        }
        //都统一用小写
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);//必须提前注册过的 才会存储
        if(var) {//将对应的值转为字符串
            FIBER_LOG_DEBUG(g_logger)<<"config "<<key;
            if(i.second.IsScalar()) {
                var->fromString(i.second.Scalar());
            } 
            else {//用字符流转换
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }    
}
void Config::LoadFromConfDir(const std::string& path,bool force){
    //后续会升级 将path用程序进行转换 先用一个最简单版本吧 就读一个
    try{
        YAML::Node root=YAML::LoadFile(path);
        LoadFromYaml(root);
        FIBER_LOG_INFO(g_logger)<<"LoadConfFile file="<<path<<" ok";
    }
    catch(...){
        FIBER_LOG_ERROR(g_logger)<<"LoadConfFile file="<<path<<" failed";
    }

}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& m = GetDatas();
    for(auto it = m.begin();
            it != m.end(); ++it) {
        cb(it->second);
    }

}
}