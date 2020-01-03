#pragma once

// 最大8组ctx来适应不同版本的、不同连接，应该足够了
#define MAX_SSL_CTX 8

#include "../../global/global.h"

struct XSSLCtx
{
    void *_ctx;
    char *_passwd;
};

class SSLMgr
{
public:
    // 自动协商版本号指在SSLv3, TLSv1, TLSv1.1 and TLSv1.2版本号中选择一个
    // SSL2、SSL3是deprecated的，仅作备用。尽量使用TLS高版本
    typedef enum
    {
        SSLVT_NONE = 0, // 无效
        SSLVT_TLS_GEN_AT =
            1, // 通用类型(generic type)，可作服务器或客户端,自动协商
        SSLVT_TLS_SRV_AT = 2, // 服务器用，自动协商版本
        SSLVT_TLS_CLT_AT = 3, // 客户端用，自动协商版本
        // SSLVT_TLS_GEN_12    = 4, // 通用类型，TLS 1.2 deprecated
        // SSLVT_SSL_GEN_23 = 4, // 通用类型，SSL2、SSL3版本
        // SSLVT_SSL_SRV_23 = 5, // 服务器用，SSL2、SSL3版本
        // SSLVT_SSL_CLT_23 = 6, // 客户端用，SSL2、SSL3版本

        SSLVT_MAX
    } SSLVT; // ssl version type

    // key 加密类型
    typedef enum
    {
        KT_NONE = 0,
        KT_GEN  = 1, // 通用类型，BEGIN PRIVATE KEY
        KT_RSA  = 2, // RSA，BEGIN RSA PRIVATE KEY

        KT_MAX
    } KeyType;

public:
    ~SSLMgr();
    explicit SSLMgr();

    /* 获取一个SSL_CTX
     * 之所以不直接返回SSL_CTX类弄，是因为不想包含巨大的openssl/ssl.h头文件
     * SSL_CTX是一个typedef，不能前置声明
     */
    void *get_ssl_ctx(int32_t idx);
    /* 创建一个ssl上下文
     * @sslv： ssl版本，见sslv_t枚举
     * @cert_file: ca证书文件路径
     * @key_file: 私钥文件路径
     */
    int32_t new_ssl_ctx(SSLVT sslv, const char *cert_file, KeyType keyt,
                        const char *key_file, const char *passwd);

private:
    int32_t _ctx_idx;
    struct XSSLCtx _ssl_ctx[MAX_SSL_CTX];
};