#include "fastdfs.h"


namespace FiberServer{
    Logger::ptr g_logger = FIBER_LOG_NAME("fastdfs");
    ConfigVar<std::string>::ptr g_conf_path = Config::Lookup<std::string>("fastdfs.conf_path",
         "/etc/fdfs/client.conf", "fastdfs conf path");

FastDFS::FastDFS(ConnectionInfo *pTrackerServer){
    m_pTrackerServer = pTrackerServer;
    m_store_index = 0;
    memset(m_group_name, 0, sizeof(m_group_name));
    memset(&m_storageServer, 0, sizeof(m_storageServer));
    // log_init();
}

FastDFS::~FastDFS(){
    if(m_pTrackerServer){
        tracker_close_connection_ex(m_pTrackerServer, true);
        m_pTrackerServer = nullptr;
    }
}

bool FastDFS::init(){
    m_store_index = 0;
    memset(m_group_name, 0, sizeof(m_group_name));
    memset(&m_storageServer, 0, sizeof(m_storageServer));

    if (tracker_query_storage_store(m_pTrackerServer, &m_storageServer, m_group_name, &m_store_index) != 0) {
        FIBER_LOG_ERROR(g_logger) << "query Storage error, errno=" << errno << ": " << strerror(errno);
        tracker_close_connection_ex(m_pTrackerServer, true);
        m_pTrackerServer = nullptr;
        return false;
    }
    return true;
}

bool FastDFS::queryStorageServer(){
    return m_storageServer.sock != -1;
}

bool FastDFS::uploadBigFile(const std::string& file_path, std::string& file_id){
    char file_id_buff[256];
    int result = storage_upload_by_filename(m_pTrackerServer, &m_storageServer,
            m_store_index, file_path.c_str(),
            NULL, NULL, 0, m_group_name, file_id_buff);
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "upload big file error, errno=" << errno
                                   << ": " << strerror(errno);
        return false;
    }
    file_id = std::string(m_group_name) + "/" + file_id_buff;
    return true;
}

bool FastDFS::uploadSmallFile(const std::string& content, std::string& file_id){
    char file_id_buff[256]; // file_id = "group/path" 格式，256 足够
    
    int result = storage_upload_by_filebuff(m_pTrackerServer, &m_storageServer,
            m_store_index, content.data(),
            content.size(), NULL, NULL, 0, m_group_name, file_id_buff);
            
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "upload small file error, result=" << result 
                                   << ", errno=" << errno << ": " << strerror(errno);
        return false;
    }


    file_id = std::string(m_group_name) + "/" + file_id_buff;
    // ------------------

    return true;
}
bool FastDFS::deleteFile(const std::string& file_id){
    int result = storage_delete_file1(m_pTrackerServer,
        NULL, file_id.c_str());
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "delete file error, errno=" << errno
                                   << ": " << strerror(errno);
        return false;
    }
    return true;
}

bool FastDFS::deleteFileByMd5(SociDB::ptr db, const std::string& md5){
    auto file_info = file_info::GetFileByMd5(db, md5);
    if(!file_info){
        FIBER_LOG_WARN(g_logger) << "file not found, md5=" << md5;
        return false;
    }
    return deleteFile(file_info->file_id);
}

std::string FastDFS::downloadFile(const std::string& file_id){
    char *file_buff = nullptr;
    int64_t file_size = 0;
    int result = storage_do_download_file1_ex(m_pTrackerServer, &m_storageServer,
            FDFS_DOWNLOAD_TO_BUFF, file_id.c_str(),
            0, 0, &file_buff, NULL, &file_size);
    if(result != 0 || file_buff == nullptr){
        FIBER_LOG_ERROR(g_logger) << "download file error, errno=" << errno
                                   << ": " << strerror(errno);
        return "";
    }
    std::string content(file_buff, file_size);
    free(file_buff);
    return content;
}

bool FastDFS::downloadFileToPath(const std::string& file_id, const std::string& local_path){
    int64_t file_size = 0;
    int result = storage_download_file_to_file1(m_pTrackerServer, &m_storageServer,
            file_id.c_str(), local_path.c_str(), &file_size);
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "download file to path error, errno=" << errno
                                   << ": " << strerror(errno);
        return false;
    }
    return true;
}

int64_t FastDFS::getFileSize(const std::string& file_id){
    FDFSFileInfo file_info;
    int result = fdfs_get_file_info1(file_id.c_str(), &file_info);
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "get file info error, errno=" << errno
                                   << ": " << strerror(errno);
        return -1;
    }
    return file_info.file_size;
}

int64_t FastDFS::getFileSizeByMd5(SociDB::ptr db, const std::string& md5){
    return file_info::GetFileSizeByMd5(db, md5);
}

std::string FastDFS::getFileUrl(const std::string& file_id){
    (void)file_id;
    return "";
}

std::string FastDFS::getFileOwnerByMd5(SociDB::ptr db, const std::string& md5){
    return file_info::GetFileOwnerByMd5(db, md5);
}

FastDFSManager::FastDFSManager(){
    m_maxConn=10;
    int result = fdfs_client_init(g_conf_path->getValue().c_str());
    // log_set_prefix_ex(NULL, NULL, NULL);
    if(result != 0){
        FIBER_LOG_ERROR(g_logger) << "fastdfs client init error, errno=" << errno
                                   << ": " << strerror(errno);
        return;
    }
    
}
FastDFSManager::~FastDFSManager(){
    MutexType::Lock lock(m_mutex);
    for(auto& i : m_conns){
        delete i;
    }
    // ????????????????
    fdfs_client_destroy();
}
void FastDFSManager::freeFastDFS(FastDFS* fastdfs){
    MutexType::Lock lock(m_mutex);
    if(m_conns.size()<m_maxConn){
        m_conns.push_back(fastdfs);
    }
    else{
        delete fastdfs;
    }
}
FastDFS::ptr FastDFSManager::get(){
    MutexType::Lock lock(m_mutex);
    if(m_conns.empty()){
        lock.unlock();
        ConnectionInfo* pTrackerServer = tracker_get_connection();
        if(!pTrackerServer){
            FIBER_LOG_ERROR(g_logger) << "tracker_get_connection failed";
            return nullptr;
        }
        FastDFS* fdfs = new FastDFS(pTrackerServer);
        if(!fdfs->init()){
            delete fdfs;
            return nullptr;
        }
        return FastDFS::ptr(fdfs,[this](FastDFS* f){
            this->freeFastDFS(f);});
    }
    FastDFS* fastdfs = m_conns.front();
    m_conns.pop_front();
    lock.unlock();
    return FastDFS::ptr(fastdfs,[this](FastDFS* fastdfs){
        this->freeFastDFS(fastdfs);});
}

bool FastDFSUtil::uploadBigFile(const std::string& file_path, std::string& file_id){
    auto fdfs = FastDFSMgr::GetInstance()->get();
    if(!fdfs) return false;
    return fdfs->uploadBigFile(file_path, file_id);
}
bool FastDFSUtil::uploadSmallFile(const std::string& content, std::string& file_id){
    auto fdfs = FastDFSMgr::GetInstance()->get();
    if(!fdfs) return false;
    return fdfs->uploadSmallFile(content, file_id);
}
bool FastDFSUtil::deleteFile(const std::string& file_id){
    return FastDFSMgr::GetInstance()->get()->deleteFile(file_id);
}
bool FastDFSUtil::deleteFileByMd5(SociDB::ptr db, const std::string& md5){
    return FastDFSMgr::GetInstance()->get()->deleteFileByMd5(db, md5);
}
std::string FastDFSUtil::downloadFile(const std::string& file_id){
    return FastDFSMgr::GetInstance()->get()->downloadFile(file_id);
}
bool FastDFSUtil::downloadFileToPath(const std::string& file_id, const std::string& local_path){
    return FastDFSMgr::GetInstance()->get()->downloadFileToPath(file_id, local_path);
}
int64_t FastDFSUtil::getFileSize(const std::string& file_id){
    return FastDFSMgr::GetInstance()->get()->getFileSize(file_id);
}
int64_t FastDFSUtil::getFileSizeByMd5(SociDB::ptr db, const std::string& md5){
    return FastDFSMgr::GetInstance()->get()->getFileSizeByMd5(db, md5);
}
std::string FastDFSUtil::getFileUrl(const std::string& file_id){
    return FastDFSMgr::GetInstance()->get()->getFileUrl(file_id);
}
std::string     FastDFSUtil::getFileOwnerByMd5(SociDB::ptr db, const std::string& md5){
    return FastDFSMgr::GetInstance()->get()->getFileOwnerByMd5(db, md5);
}
}
