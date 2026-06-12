#include "chunkManager.h"
#include "FiberServer/base/log.h"
#include "FiberServer/base/util.h"

namespace FiberServer {

static Logger::ptr g_logger = FIBER_LOG_NAME("uploadfiles");
// ==================== 静态成员初始化 ====================

static ConfigVar<std::string>::ptr g_tmpRoot = Config::Lookup<std::string>(
    "uploadfiles.tmp_path", "/var/data/tmp_uploads", "tmp path");
static ConfigVar<uint64_t>::ptr g_cleanTime = Config::Lookup<uint64_t>(
    "uploadfiles.clean_time", 3600, "clean time(s)");
static ConfigVar<uint64_t>::ptr g_chunkSizes = Config::Lookup<uint64_t>(
    "uploadfiles.chunk_sizes", 5242880, "chunk sizes(bytes)");
static ConfigVar<uint64_t>::ptr g_lastTime = Config::Lookup<uint64_t>(
    "uploadfiles.last_time", 7200, "last time(s)");


// ==================== 内部方法 ====================

ChunkManager::ChunkManager() {
        init();
}

ChunkManager::~ChunkManager() {
    m_cleanTimer->cancel();
}
void ChunkManager::init() {
  
    // 启动定时清理任务
    m_cleanTimer = IOManager::GetThis()->addTimer(
        
        g_cleanTime->getValue() * 1000,
        []() { cleanTask(); },
        true);
        
    //     g_cleanTime->addListener([this](const uint64_t& old_value,const uint64_t& new_value){
    //     m_cleanTimer->reset(new_value * 1000, true);
    // });
}

std::string ChunkManager::buildTaskPath(const std::string& username, const std::string& md5) {
    return g_tmpRoot->getValue() + "/" + username + "/" + md5;
}
std::string ChunkManager::buildChunkPath(const std::string& username, const std::string& md5,int index) {
    return buildTaskPath(username, md5) + "/" + std::to_string(index);
}
// ==================== 配置方法 ====================

int64_t ChunkManager::getChunkSizes() {
    
    return g_chunkSizes->getValue();
}

const std::string& ChunkManager::getTmpRoot() {
    return g_tmpRoot->getValue();
}

// ==================== 分片操作 ====================

std::vector<int> ChunkManager::getUploadedChunks(const std::string& username, const std::string& md5) {
    std::string path = buildTaskPath(username, md5);
    return FSUtil::GetIndices(path);
}

bool ChunkManager::saveChunk(const std::string& tmp_file_path, const std::string& username, 
                             const std::string& md5, int index) {

    
        std::string path = buildChunkPath(username, md5,index);
    
    // 移动文件到任务目录
    if (!FSUtil::Mv(tmp_file_path, path)) {
        FIBER_LOG_ERROR(g_logger) << "saveChunk error, mv failed,to_path=" << path 
                                         << ", from_path=" << tmp_file_path;
        return false;
    }
    
    // 重命名为分片编号
    // std::string old_name = FSUtil::Basename(tmp_file_path);
    // std::string new_name = std::to_string(index);
    
    // if (!FSUtil::Mv(path + "/" + old_name, path + "/" + new_name)) {
    //     FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "saveChunk error, mv failed, path=" << path 
    //                                      << ", old_name=" << old_name 
    //                                      << ", new_name=" << new_name;
    //     return false;
    // }
    
    return true;
}

bool ChunkManager::isAllChunksReady(const std::string& username, const std::string& md5, int total_chunks) {
    std::string path = buildTaskPath(username, md5);
    std::vector<int> indices = FSUtil::GetIndices(path);
    return indices.size() == total_chunks;
}

std::string ChunkManager::mergeChunks(const std::string& username, const std::string& md5, int total_chunks) {
    
    
    std::string path = buildTaskPath(username, md5);
    std::vector<int> indices = FSUtil::GetIndices(path);
    
    if (indices.size() != total_chunks) {
        FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "mergeChunks error, chunks not ready, expected=" << total_chunks
                                         << ", actual=" << indices.size();
        return "";
    }
    
    std::string mergedFilePath = path + "/" + md5;
    if (!FSUtil::MergeFiles(mergedFilePath, path, indices)) {
        FIBER_LOG_ERROR(FIBER_LOG_ROOT()) << "mergeChunks error, merge failed, path=" << path;
        return "";
    }
    
    return mergedFilePath;
}

// ==================== 清理任务 ====================

void ChunkManager::cleanTask() {
    FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "cleanTask start";
    std::string path = g_tmpRoot->getValue();
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, path, "");
    
    int64_t deadLine = GetCurrentMS()/1000 - g_lastTime->getValue();
    int cleaned = 0;
    
    // 第一步：删除过期文件
    for (auto& file : files) {
        if (FSUtil::IsDir(file)) {
            continue;
        }
        if (FSUtil::GetFileTime(file) < deadLine) {
            if (FSUtil::Rm(file)) {
                ++cleaned;
            }
        }
    }
    
    // 第二步：删除所有空的任务目录（/var/data/tmp_uploads/用户名/MD5/）
    std::vector<std::string> dirs;
    FSUtil::ListAllDir(dirs, path, "");
    for (auto& dir : dirs) {
        std::string taskDir = dir;
        std::vector<std::string> subdirs;
        FSUtil::ListAllDir(subdirs, taskDir, "");
        for (auto& subdir : subdirs) {
            // subdir = /var/data/tmp_uploads/用户名/MD5/
            if (FSUtil::IsDirEmpty(subdir)) {
                FSUtil::RmEmptyDir(subdir);
                ++cleaned;
            }
        }
    }
   
    if (cleaned > 0) {
        FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "cleanTask: cleaned " << cleaned << " expired files/dirs";
    }
}



} // namespace FiberServer
