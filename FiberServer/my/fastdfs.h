#pragma once

#include <string>
#include <memory>
#include <list>
#include <string>
#include <unistd.h>
#include <cerrno>
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/singleton.h"
#include "FiberServer/base/mutex.h"
#include "FiberServer/base/log.h"
#include "FiberServer/my/mysqlop.h"
#include "FiberServer/base/config.h"
#include <fastdfs/client_global.h>
#include <fastdfs/fdfs_client.h>
#include <fastdfs/storage_client1.h>
#include <fastdfs/tracker_client.h>
#include <fastdfs/fdfs_global.h>
#include <fastcommon/logger.h>
namespace FiberServer{

class FastDFS{
public:
    typedef std::shared_ptr<FastDFS> ptr;

    FastDFS(ConnectionInfo *pTrackerServer);
    ~FastDFS();

    bool init();

    bool uploadBigFile(const std::string& file_path, std::string& file_id);

    bool uploadSmallFile(const std::string& content, std::string& file_id);

    bool deleteFile(const std::string& file_id);
    bool deleteFileByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    bool deleteFileByMd5(SociDB::ptr db, const std::string& md5);
#endif

    std::string downloadFile(const std::string& file_id);
    bool downloadFileToPath(const std::string& file_id, const std::string& local_path);

    int64_t getFileSize(const std::string& file_id);
    int64_t getFileSizeByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    int64_t getFileSizeByMd5(SociDB::ptr db, const std::string& md5);
#endif

    std::string getFileUrl(const std::string& file_id);
    std::string getFileUrlByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    std::string getFileUrlByMd5(SociDB::ptr db, const std::string& md5);
#endif

private:
    bool queryStorageServer();

private:
    ConnectionInfo* m_pTrackerServer;
    ConnectionInfo m_storageServer;
    int m_store_index;
    char m_group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
};
class FastDFSManager{
public:
    typedef FiberServer::Mutex MutexType;
    FastDFSManager();
    ~FastDFSManager();
    FastDFS::ptr get();
    uint32_t getMaxConn() const { return m_maxConn;}
private:
    void freeFastDFS(FastDFS* fastdfs);
private:
    std::list<FastDFS*> m_conns;
    MutexType m_mutex;
    uint32_t m_maxConn;
};
typedef Singleton<FastDFSManager> FastDFSMgr;

class FastDFSUtil{
public:
    static bool uploadBigFile(const std::string& file_path, std::string& file_id);
    static bool uploadSmallFile(const std::string& content, std::string& file_id);
    static bool deleteFile(const std::string& file_id);
    static bool deleteFileByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    static bool deleteFileByMd5(SociDB::ptr db, const std::string& md5);
#endif
    static std::string downloadFile(const std::string& file_id);
    static bool downloadFileToPath(const std::string& file_id, const std::string& local_path);
    static int64_t getFileSize(const std::string& file_id);
    static int64_t getFileSizeByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    static int64_t getFileSizeByMd5(SociDB::ptr db, const std::string& md5);
#endif
    static std::string getFileUrl(const std::string& file_id);
    static std::string getFileUrlByMd5(MySQL::ptr db, const std::string& md5);
#ifdef FIBERSERVER_USE_SOCI
    static std::string getFileUrlByMd5(SociDB::ptr db, const std::string& md5);
#endif
};
}
