#pragma once

#include "codec.hpp"

class lflatbuffers;
class FlatbuffersCodec final : public Codec
{
public:
    FlatbuffersCodec();
    ~FlatbuffersCodec();

    void finalize() override;
    void reset() override;
    int32_t load_path(const char *path) override;
    int32_t load_file(const char *path) override;

    /* 解码数据包
     * return: <0 error,otherwise the number of parameter push to stack
     */
    int32_t decode(lua_State *L, const char *buffer, size_t len,
                   const CmdCfg *cfg) override;
    /* 编码数据包
     * return: <0 error,otherwise the length of buffer
     */
    int32_t encode(lua_State *L, int32_t index, const char **buffer,
                   const CmdCfg *cfg) override;

private:
    class lflatbuffers *_lflatbuffers;
};
