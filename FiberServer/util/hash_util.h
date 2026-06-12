#pragma once 
#include <stdint.h>
#include <string>
#include <vector>

namespace FiberServer {

/**
 * @section 高性能非加密哈希 (MurmurHash3)
 * 特点：速度极快，碰撞率低。常用于逻辑层、散列表（HashMap）、负载均衡、分布式分片。
 * 注意：不可逆，且不具备加密安全性（不能用来存密码）。
 */
uint32_t murmur3_hash(const char * str, const uint32_t & seed = 1060627423);
uint64_t murmur3_hash64(const char * str, const uint32_t & seed = 1060627423, const uint32_t& seed2 = 1050126127);
uint32_t murmur3_hash(const void* str, const uint32_t& size, const uint32_t & seed = 1060627423);
uint64_t murmur3_hash64(const void* str, const uint32_t& size, const uint32_t & seed = 1060627423, const uint32_t& seed2 = 1050126127);

/**
 * @section 极速哈希 (QuickHash)
 * 追求极致速度的简单哈希算法。
 */
uint32_t quick_hash(const char * str);
uint32_t quick_hash(const void* str, uint32_t size);

/**
 * @section Base64 编解码
 * 用于将二进制数据转为可见字符。
 * 业务用途：Token 生成后的编码传输。
 */
std::string base64decode(const std::string &src);
std::string base64encode(const std::string &data);
std::string base64encode(const void *data, size_t len);

/**
 * @section 经典加密哈希 (返回 16 进制字符串)
 * 业务用途：md5 就是你流程里要用的那个“后端加盐哈希”。
 * 返回值：例如 "5d41402abc..." 这种 32 位字符串。
 */
std::string md5(const std::string &data);
std::string sha1(const std::string &data);

/**
 * @section 加密哈希原始结果 (返回 Blob/二进制)
 * sum 系列返回的是哈希计算后的原始二进制字节流，而不是 16 进制字符。
 */
std::string md5sum(const std::string &data);
std::string md5sum(const void *data, size_t len);
// std::string sha0sum(const std::string &data);
// std::string sha0sum(const void *data, size_t len);
std::string sha1sum(const std::string &data);
std::string sha1sum(const void *data, size_t len);

/**
 * @section HMAC 密钥散列消息认证码
 * 比简单的 md5(salt + pass) 更安全的加盐算法。
 * 它使用一个 key (salt) 对 text 进行哈希，防止长度扩展攻击。
 */
std::string hmac_md5(const std::string &text, const std::string &key);
std::string hmac_sha1(const std::string &text, const std::string &key);
std::string hmac_sha256(const std::string &text, const std::string &key);

/**
 * @section 数据与 16 进制字符串互相转换
 * 例如：将内存中的 {0xFF, 0x01} 转为字符串 "ff01"。
 */
void hexstring_from_data(const void *data, size_t len, char *output);
std::string hexstring_from_data(const void *data, size_t len);
std::string hexstring_from_data(const std::string &data);

void data_from_hexstring(const char *hexstring, size_t length, void *output);
std::string data_from_hexstring(const char *hexstring, size_t length);
std::string data_from_hexstring(const std::string &data);

/**
 * @section 常用字符串处理工具
 */
// 替换字符串中的字符/子串
std::string replace(const std::string &str, char find, char replaceWith);
std::string replace(const std::string &str, char find, const std::string &replaceWith);
std::string replace(const std::string &str, const std::string &find, const std::string &replaceWith);

// 按照分隔符拆分字符串 (返回数组)
std::vector<std::string> split(const std::string &str, char delim, size_t max = ~0);
std::vector<std::string> split(const std::string &str, const char *delims, size_t max = ~0);

/**
 * @section 随机字符串生成
 * 业务用途：注册时生成用户专属的“随机盐 (Salt)”。
 * @param len 生成的长度
 * @param chars 备选字符集，默认是数字和大小写字母
 */
std::string random_string(size_t len
        ,const std::string& chars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}
