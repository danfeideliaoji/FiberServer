#pragma once
#include<byteswap.h>
#include<stdint.h>

#define FIBER_LITTLE_ENDIAN 1 //小段
#define FIBER_BIG_ENDIAN 2 // 大段

namespace FiberServer{
    //这里把sylar原来的字节转换写一起了 一定要用if constexpr（编译时确定）
    template<typename T>
    T byteswap(T value){
        if constexpr(sizeof(T)==sizeof(uint16_t)){
            return (T)bswap_16(uint16_t(value));
        }
        else if constexpr(sizeof(T)==sizeof(uint32_t)){
            return (T)bswap_32(uint32_t(value));
        }
        else if constexpr(sizeof(T)==sizeof(uint64_t)){
            return (T)bswap_64(uint64_t(value));
        }
        else{
            return value;
        }
    } 
#if BYTE_ORDER == LITTLE_ENDIAN  // 当前机器为小端
#define FIBER_BYTE_ORDER FIBER_LITTLE_ENDIAN
#else //为大端
#define  FIBER_BYTE_ORDER FIBER_BIG_ENDIAN
#endif

#if FIBER_BYTE_ORDER == FIBER_BIG_ENDIAN

/**
 * @brief 只在小端机器上执行byteswap, 在大端机器上什么都不做
 */
template<class T>
T byteswapOnLittleEndian(T t) {
    return t;
}

/**
 * @brief 只在大端机器上执行byteswap, 在小端机器上什么都不做
 */
template<class T>
T byteswapOnBigEndian(T t) {
    return byteswap(t);
}
#else

/**
 * @brief 只在小端机器上执行byteswap, 在大端机器上什么都不做
 */
template<class T>
T byteswapOnLittleEndian(T t) {
    return byteswap(t);
}

/**
 * @brief 只在大端机器上执行byteswap, 在小端机器上什么都不做
 */
template<class T>
T byteswapOnBigEndian(T t) {
    return t;
}
#endif
}