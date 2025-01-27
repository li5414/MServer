#include "flatbuffers_codec.hpp"

#include "../net_header.hpp"
#include <lflatbuffers.hpp>

FlatbuffersCodec::FlatbuffersCodec()
{
    _lflatbuffers = new class lflatbuffers();
}

FlatbuffersCodec::~FlatbuffersCodec()
{
    delete _lflatbuffers;
    _lflatbuffers = nullptr;
}

void FlatbuffersCodec::finalize() {}

void FlatbuffersCodec::reset()
{
    delete _lflatbuffers;
    _lflatbuffers = new class lflatbuffers();
}

int32_t FlatbuffersCodec::load_path(const char *path)
{
    return _lflatbuffers->load_bfbs_path(path);
}

int32_t FlatbuffersCodec::load_file(const char *path)
{
    return _lflatbuffers->load_bfbs_file(path) ? 0 : -1;
}

/* 解码数据包
 * return: <0 error,otherwise the number of parameter push to stack
 */
int32_t FlatbuffersCodec::decode(lua_State *L, const char *buffer, size_t len,
                                 const CmdCfg *cfg)
{
    if (_lflatbuffers->decode(L, cfg->_schema, cfg->_object, buffer, len) < 0)
    {
        ELOG("flatbuffers decode:%s", _lflatbuffers->last_error());
        return -1;
    }

    // 默认情况下，decode把所有内容解析到一个table
    return 1;
}

/* 编码数据包
 * return: <0 error,otherwise the length of buffer
 */
int32_t FlatbuffersCodec::encode(lua_State *L, int32_t index,
                                 const char **buffer, const CmdCfg *cfg)
{
    if (_lflatbuffers->encode(L, cfg->_schema, cfg->_object, index) < 0)
    {
        ELOG("flatbuffers encode:%s", _lflatbuffers->last_error());
        return -1;
    }

    size_t size = 0;
    *buffer     = _lflatbuffers->get_buffer(size);

    return static_cast<int32_t>(size);
}
