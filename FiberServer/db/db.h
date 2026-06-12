#pragma once
#include <memory>
#include <string>

namespace FiberServer{

// SQL查询结果集接口
class ISQLData {
public:
    typedef std::shared_ptr<ISQLData> ptr;
    virtual ~ISQLData() {}

    // 获取错误码和错误信息
    virtual int getErrno() const = 0;
    virtual const std::string& getErrStr() const = 0;

    // 获取结果集元数据 (数据行数、列数、某列的字节数/类型/列名)
    virtual int getDataCount() = 0;
    virtual int getColumnCount() = 0;
    virtual int getColumnBytes(int idx) = 0;
    virtual int getColumnType(int idx) = 0;
    virtual std::string getColumnName(int idx) = 0;

    // 判断某列是否为空
    virtual bool isNull(int idx) = 0;
    
    // 统一的数据获取接口：根据索引 idx 获取对应数据类型的值
    virtual int8_t getInt8(int idx) = 0;
    virtual uint8_t getUint8(int idx) = 0;
    virtual int16_t getInt16(int idx) = 0;
    virtual uint16_t getUint16(int idx) = 0;
    virtual int32_t getInt32(int idx) = 0;
    virtual uint32_t getUint32(int idx) = 0;
    virtual int64_t getInt64(int idx) = 0;
    virtual uint64_t getUint64(int idx) = 0;
    virtual float getFloat(int idx) = 0;
    virtual double getDouble(int idx) = 0;
    virtual std::string getString(int idx) = 0;
    virtual std::string getBlob(int idx) = 0;
    virtual time_t getTime(int idx) = 0;
    
    // 游标移动到下一条记录
    virtual bool next() = 0;
};

// SQL更新接口 (用于执行 INSERT/UPDATE/DELETE 等)
class ISQLUpdate {
public:
    virtual ~ISQLUpdate() {}
    // 执行SQL更新语句，支持C风格格式化或纯字符串
    virtual int execute(const char* format, ...) = 0;
    virtual int execute(const std::string& sql) = 0;
    // 获取最后一次插入操作自增的主键ID
    virtual int64_t getLastInsertId() = 0;
};

// SQL查询接口 (用于执行 SELECT)
class ISQLQuery {
public:
    virtual ~ISQLQuery() {}
    // 执行SQL查询语句，返回结果集对象
    virtual ISQLData::ptr query(const char* format, ...) = 0;
    virtual ISQLData::ptr query(const std::string& sql) = 0;
};

// SQL预编译语句接口 (Prepared Statement)
class IStmt {
public:
    typedef std::shared_ptr<IStmt> ptr;
    virtual ~IStmt(){}
    
    // 统一的参数绑定接口：将各类型的值绑定到指定的索引 idx
    virtual int bindInt8(int idx, const int8_t& value) = 0;
    virtual int bindUint8(int idx, const uint8_t& value) = 0;
    virtual int bindInt16(int idx, const int16_t& value) = 0;
    virtual int bindUint16(int idx, const uint16_t& value) = 0;
    virtual int bindInt32(int idx, const int32_t& value) = 0;
    virtual int bindUint32(int idx, const uint32_t& value) = 0;
    virtual int bindInt64(int idx, const int64_t& value) = 0;
    virtual int bindUint64(int idx, const uint64_t& value) = 0;
    virtual int bindFloat(int idx, const float& value) = 0;
    virtual int bindDouble(int idx, const double& value) = 0;
    virtual int bindString(int idx, const char* value) = 0;
    virtual int bindString(int idx, const std::string& value) = 0;
    virtual int bindBlob(int idx, const void* value, int64_t size) = 0;
    virtual int bindBlob(int idx, const std::string& value) = 0;
    virtual int bindTime(int idx, const time_t& value) = 0;
    virtual int bindNull(int idx) = 0;

    // 执行预编译的更新语句
    virtual int execute() = 0;
    virtual int64_t getLastInsertId() = 0;
    // 执行预编译的查询语句，返回结果集
    virtual ISQLData::ptr query() = 0;

    // 获取错误信息
    virtual int getErrno() = 0;
    virtual std::string getErrStr() = 0;
};

// 数据库事务接口，继承自更新接口 要么全部执行成功，要么全部不执行（撤销），绝对不会出现只执行了一半的情况
class ITransaction : public ISQLUpdate {
public:
    typedef std::shared_ptr<ITransaction> ptr;
    virtual ~ITransaction() {};
    
    // 开始、提交、回滚事务
    virtual bool begin() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;
};

// 数据库核心操作总接口，包含读写能力
class IDB : public ISQLUpdate
            ,public ISQLQuery {
public:
    typedef std::shared_ptr<IDB> ptr;
    virtual ~IDB() {}

    // 创建一个预编译语句对象
    virtual IStmt::ptr prepare(const std::string& stmt) = 0;
    
    virtual int getErrno() = 0;
    virtual std::string getErrStr() = 0;
    
    // 开启并获取一个事务对象 (auto_commit指定是否自动提交)
    virtual ITransaction::ptr openTransaction(bool auto_commit = false) = 0;
};

}