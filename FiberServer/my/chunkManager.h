#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "FiberServer/base/config.h"
#include "FiberServer/iomanager.h"

namespace FiberServer {

/**
 * @brief ChunkManager - 分片管理器（静态方法类）
 * @note 所有方法均为静态方法，无需创建实例
 *       自动管理定时清理任务，首次调用时初始化
 */
class ChunkManager {
public:
    // ==================== 配置相关 ====================
    ChunkManager();
    ~ChunkManager();
    /// 获取分片大小（字节）
    static int64_t getChunkSizes();
    
    /// 获取临时文件根目录
    static const std::string& getTmpRoot();

    // ==================== 分片操作 ====================

    /**
     * @brief 获取已上传的分片编号列表（用于前端预检/断点续传）
     * @param username 用户名
     * @param md5 文件的唯一标识
     * @return 已上传的分片编号列表
     */
    static std::vector<int> getUploadedChunks(const std::string& username, const std::string& md5);

    /**
     * @brief 移动 Nginx 暂存的分片到对应的 MD5 任务目录
     * @param tmp_file_path Nginx 传来的 X-File-Path
     * @param username 用户名
     * @param md5 文件的唯一标识
     * @param index 当前分片的编号
     * @return 是否移动成功
     */
    static bool saveChunk(const std::string& tmp_file_path, const std::string& username, 
                          const std::string& md5, int index);

    /**
     * @brief 检查是否所有分片都到齐了
     * @param username 用户名
     * @param md5 文件的唯一标识
     * @param total_chunks 总分片数
     * @return 是否所有分片都已就绪
     */
    static bool isAllChunksReady(const std::string& username, const std::string& md5, int total_chunks);

    /**
     * @brief 合并所有分片并返回合并后的完整文件路径
     * @param username 用户名
     * @param md5 文件的唯一标识
     * @param total_chunks 总分片数
     * @return 合并成功返回绝对路径，失败返回空字符串
     */
    static std::string mergeChunks(const std::string& username, const std::string& md5, int total_chunks);

    /**
     * @brief 清理过期的临时文件
     * @note 由定时器自动调用，也可手动调用
     */
    static void cleanTask();

    // ==================== 初始化 ====================
    
  
    
    /// 构建任务目录路径
    static std::string buildTaskPath(const std::string& username, const std::string& md5);
    static std::string buildChunkPath(const std::string& username, const std::string& md5,int index);
private:
    /// 内部初始化（仅调用一次）
     void init();
    

    // ==================== 静态成员 ====================              // 是否已初始化
     Timer::ptr m_cleanTimer;          // 清理定时器
};

} // namespace FiberServer
