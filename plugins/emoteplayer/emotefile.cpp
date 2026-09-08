#include "emotefile.h"

#include "TVPStorage.h"
#include "UtilStreams.h"
#include "Platform.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#include "xp3filter.h"

#include <zlib.h>

#include <sstream>
#include <algorithm>

struct EMoteCTX
{
    tjs_uint32 key[4];
    tjs_uint32 v;
    tjs_uint32 count;
};

inline void init_emote_ctx(EMoteCTX* ctx, const tjs_uint32 key[4])
{
    ctx->key[0] = key[0];
    ctx->key[1] = key[1];
    ctx->key[2] = key[2];
    ctx->key[3] = key[3];
    ctx->v = 0;
    ctx->count = 0;
}

inline void emote_decrypt(EMoteCTX* ctx, tjs_uint8* data, tjs_uint32 size)
{
    for (tjs_uint32 i = 0; i < size; i++)
    {
        if (!ctx->v)
        {
            tjs_uint32 b = ctx->key[3];
            tjs_uint32 a = ctx->key[0] ^ (ctx->key[0] << 0xB);
            ctx->key[0] = ctx->key[1];
            ctx->key[1] = ctx->key[2];
            tjs_uint32 c = a ^ b ^ ((a ^ (b >> 0xB)) >> 8);
            ctx->key[2] = b;
            ctx->key[3] = c;
            ctx->v = c;
            ctx->count = 4;
        }

        data[i] ^= static_cast<tjs_uint8>(ctx->v);
        ctx->v >>= 8;
        ctx->count--;
    }
}

// lzfs.dll 大概也只有emote会用
namespace Lz4Stream
{

struct Lz4FrameInfo
{
    int BlockSize = 0;
    bool IndependentBlocks = false;
    bool HasBlockChecksum = false;
    bool HasContentLength = false;
    bool HasContentChecksum = false;
    bool HasDictionary = false;
    long OriginalLength = 0;
    int DictionaryId = 0;

    Lz4FrameInfo(tTJSBinaryStream* input)
    {
        // flags
        input->SetPosition(4);
        tjs_uint8 flags = input->ReadI8LE();
        int version = flags >> 6;
        if (version != 1)
            throw "Invalid LZ4 compressed stream.";
        IndependentBlocks = 0 != (flags & 0x20);
        HasBlockChecksum = 0 != (flags & 0x10);
        HasContentLength = 0 != (flags & 8);
        HasContentChecksum = 0 != (flags & 4);
        HasDictionary = 0 != (flags & 1);

        // blocksize
        tjs_int code = input->ReadI8LE();
        switch ((code >> 4) & 7)
        {
            case 4:
                BlockSize = 0x10000;
                break;
            case 5:
                BlockSize = 0x40000;
                break;
            case 6:
                BlockSize = 0x100000;
                break;
            case 7:
                BlockSize = 0x400000;
                break;
            default:
                throw "Invalid LZ4 compressed stream.";
        }

        // other
        if (HasContentLength)
        {
            tjs_uint64 length = (tjs_uint64)input->ReadI32LE();
            tjs_uint32 lengthex = input->ReadI32LE();
            length |= (tjs_uint64)lengthex << 32;
            OriginalLength = length;
        }
        if (HasDictionary)
        {
            DictionaryId = input->ReadI32LE();
        }
        input->ReadI8LE(); // skip descriptor checksum
    }
};

static const int MinMatch = 4;
static const int LastLiterals = 5;
static const int MFLimit = 12;
static const int MatchLengthBits = 4;
static const int MatchLengthMask = 0xF;
static const int RunMask = 0xF;
static void CopyOverlapped(tjs_uint8* data, int src, int dst, int count)
{
    if (count <= 0)
        return;

    if (dst > src)
    {
        while (count > 0)
        {
            int chunk = std::min(dst - src, count);
            memcpy(data + dst, data + src, chunk);
            dst += chunk;
            count -= chunk;
        }
    }
    else
    {
        memcpy(data + dst, data + src, count);
    }
}
static int DecompressBlock(tjs_uint8* block,
                           tjs_int32 block_size,
                           tjs_uint8* output,
                           tjs_int32 output_size)
{
    int src = 0;
    int iend = block_size;

    int dst = 0;
    int oend = output_size;

    for (;;)
    {
        /* get literal length */
        int token = block[src++];
        int length = token >> MatchLengthBits;
        if (RunMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                length += n;
            } while ((src < iend - RunMask) && (0xFF == n));
            if (dst + length < dst || src + length < src) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }

        /* copy literals */
        int copy_end = dst + length;
        if ((copy_end > oend - MFLimit) || (src + length > iend - (3 + LastLiterals)))
        {
            if ((src + length != iend) || copy_end > oend)
                throw "Invalid LZ4 compressed stream.";
            memcpy(output + dst, block + src, length);
            src += length;
            dst += length;
            break;
        }
        memcpy(output + dst, block + src, length);
        src += length;
        dst = copy_end;

        /* get offset */
        int offset = 0;
        tjs_uint16 recOffset = 0;
        memcpy(&recOffset, block + src, 2);
        offset = recOffset;
        src += 2;
        int match = dst - offset;
        if (match < 0)
            throw "Invalid LZ4 compressed stream.";

        /* get matchlength */
        length = token & MatchLengthMask;
        if (MatchLengthMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                if (src > iend - LastLiterals)
                    throw "Invalid LZ4 compressed stream.";
                length += n;
            } while (0xFF == n);
            if (dst + length < dst) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }
        length += MinMatch;

        /* copy match within block */
        CopyOverlapped(output, match, dst, length);
        dst += length;
    }
    return dst; // number of output bytes decoded
}

tTJSBinaryStream* GetLz4Stream(tTJSBinaryStream* orgStream)
{
    // 获取头信息
    Lz4Stream::Lz4FrameInfo info(orgStream);
    tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, info.OriginalLength);
    // 一个Block一个Block地获取数据
    bool m_eof = false;
    tjs_int m_dataBuffSize = info.BlockSize;
    tjs_uint8* m_dataBuff = new tjs_uint8[m_dataBuffSize];
    memset(m_dataBuff, 0, m_dataBuffSize);
    tjs_int m_data_size = 0;
    tjs_int m_blockBuffSize = 0;
    tjs_uint8* m_blockBuff = NULL;
    while (!m_eof)
    {
        // 获取下一个block
        tjs_int32 block_size = 0;
        if (4 != orgStream->Read(&block_size, 4))
            throw "EndOfStreamException";
        if (0 == block_size)
        {
            m_eof = true;
            m_data_size = 0;
            if (info.HasContentChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else if (block_size < 0)
        {
            m_data_size = block_size & 0x7FFFFFFF;
            if (m_dataBuff == NULL || m_data_size > m_dataBuffSize)
            {
                if (m_dataBuff != NULL)
                    delete[] m_dataBuff;
                m_dataBuff = new tjs_uint8[m_data_size];
                m_dataBuffSize = m_data_size;
                memset(m_dataBuff, 0, m_dataBuffSize);
            }
            m_data_size = orgStream->Read(m_dataBuff, m_data_size);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else
        {
            tjs_int32 m_block_size = block_size;
            if (m_blockBuff == NULL || m_block_size > m_blockBuffSize)
            {
                if (m_blockBuff != NULL)
                    delete[] m_blockBuff;
                m_blockBuff = new tjs_uint8[m_block_size];
                m_blockBuffSize = m_block_size;
                memset(m_blockBuff, 0, m_blockBuffSize);
            }
            if (m_block_size != orgStream->Read(m_blockBuff, m_block_size))
                throw "EndOfStreamException";
            m_data_size =
                Lz4Stream::DecompressBlock(m_blockBuff, m_block_size, m_dataBuff, m_dataBuffSize);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }

        // 写入数据
        _stream->Write(m_dataBuff, m_data_size);
    }
    // 删除原始并返回
    if (m_dataBuff != NULL)
        delete[] m_dataBuff;
    if (m_blockBuff != NULL)
        delete[] m_blockBuff;
    delete orgStream;
    return _stream;
}

} // namespace Lz4Stream

using namespace PSB;

namespace emoteplayer
{
#pragma region Base

emoteframe::emoteframe(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // time type
    filePtr->parseReal(time, _rootData["time"]);
    int64_t tmp;
    filePtr->parseNumber(tmp, _rootData["type"]);
    type = static_cast<int8_t>(tmp);
    // content
    auto it = _rootData.find("content");
    if (it != _rootData.end())
    {
        hasContent = true;
        filePtr->updateSyncTime(time);
        filePtr->parseObject(_rootData, it->second);
        // coord angle s z zcc
        it = _rootData.find("coord");
        if (it != _rootData.end())
        {
            std::vector<uint32_t> _listOffset;
            filePtr->parseList(_listOffset, it->second);
            if (_listOffset.size() == 3)
            {
                filePtr->parseReal(coordX, _listOffset[0]);
                filePtr->parseReal(coordY, _listOffset[1]);
                filePtr->parseReal(coordZ, _listOffset[2]);
                filePtr->updateZMax(coordZ);
            }
        }
        it = _rootData.find("angle");
        if (it != _rootData.end())
        {
            filePtr->parseReal(angle, it->second);
        }
        it = _rootData.find("sx");
        if (it != _rootData.end())
        {
            filePtr->parseReal(sx, it->second);
        }
        it = _rootData.find("sy");
        if (it != _rootData.end())
        {
            filePtr->parseReal(sy, it->second);
        }
        it = _rootData.find("zx");
        if (it != _rootData.end())
        {
            filePtr->parseReal(zx, it->second);
        }
        it = _rootData.find("zy");
        if (it != _rootData.end())
        {
            filePtr->parseReal(zy, it->second);
        }
        it = _rootData.find("ox");
        if (it != _rootData.end())
        {
            filePtr->parseReal(ox, it->second);
        }
        it = _rootData.find("oy");
        if (it != _rootData.end())
        {
            filePtr->parseReal(oy, it->second);
        }
        it = _rootData.find("zcc");
        if (it != _rootData.end())
        {
            std::map<std::string, uint32_t> zcc_map;
            if (filePtr->parseObject(zcc_map, it->second))
            {
                haszcc = true;
                // c
                std::vector<uint32_t> zcc_list;
                filePtr->parseList(zcc_list, zcc_map["c"]);
                if (zcc_list.size() == 2)
                {
                    for (int i = 0; i < 2; i++)
                        filePtr->parseReal(zcc_c[i], zcc_list[i]);
                }
                // x
                zcc_list.clear();
                filePtr->parseList(zcc_list, zcc_map["x"]);
                if (zcc_list.size() == 4)
                {
                    for (int i = 0; i < 4; i++)
                        filePtr->parseReal(zcc_x[i], zcc_list[i]);
                }
                // y
                zcc_list.clear();
                filePtr->parseList(zcc_list, zcc_map["y"]);
                if (zcc_list.size() == 4)
                {
                    for (int i = 0; i < 4; i++)
                        filePtr->parseReal(zcc_y[i], zcc_list[i]);
                }
            }
        }
        // ccc
        it = _rootData.find("ccc");
        if (it != _rootData.end())
        {
            std::map<std::string, uint32_t> ccc_map;
            if (filePtr->parseObject(ccc_map, it->second))
            {
                hasccc = true;
                // c
                std::vector<uint32_t> ccc_list;
                filePtr->parseList(ccc_list, ccc_map["c"]);
                if (ccc_list.size() == 2)
                {
                    for (int i = 0; i < 2; i++)
                        filePtr->parseReal(ccc_c[i], ccc_list[i]);
                }
                // x
                ccc_list.clear();
                filePtr->parseList(ccc_list, ccc_map["x"]);
                if (ccc_list.size() == 4)
                {
                    for (int i = 0; i < 4; i++)
                        filePtr->parseReal(ccc_x[i], ccc_list[i]);
                }
                // y
                ccc_list.clear();
                filePtr->parseList(ccc_list, ccc_map["y"]);
                if (ccc_list.size() == 4)
                {
                    for (int i = 0; i < 4; i++)
                        filePtr->parseReal(ccc_y[i], ccc_list[i]);
                }
            }
        }
        // src
        if (filePtr->isKrkr)
        {
            filePtr->parseString(src, _rootData["src"]);
        }
        else
        {
            it = _rootData.find("src");
            if (it == _rootData.end()) // 说明此时是layout
            {
                src = "layout";
            }
            else // 否则按照旧规则直接拼接吧
            {
                std::string tmp;
                // 初始为src，然后再看看是否为motion
                src = "src/";
                it = _rootData.find("motion");
                if (it != _rootData.end())
                {
                    src = "motion/";
                    // 获取timeoffset
                    std::map<std::string, uint32_t> tmpMap;
                    if (filePtr->parseObject(tmpMap, it->second))
                    {
                        it = tmpMap.find("timeOffset");
                        if (it != tmpMap.end())
                        {
                            filePtr->parseReal(timeOffset, it->second);
                        }
                    }
                }
                filePtr->parseString(tmp, _rootData["src"]);
                // 还有一个blank情形
                if (strcmp(tmp.c_str(), "blank") == 0)
                {
                    src = "";
                }
                src += tmp;
                filePtr->parseString(tmp, _rootData["icon"]);
                src += "/" + tmp;
                src.erase(std::remove(src.begin(), src.end(), '\0'), src.end());
            }
        }
        // mask
        filePtr->parseNumber(mask, _rootData["mask"]);
        // bm
        it = _rootData.find("bm");
        if (it != _rootData.end())
        {
            filePtr->parseNumber(bm, it->second);
        }
        // color
        it = _rootData.find("color");
        if (it != _rootData.end())
        {
            // 只有 PSB 真正写了 color 才置位。缺省 0 会被当成黑色调制。
            hasColor = filePtr->parseNumber(color, it->second);
        }
        // opa
        it = _rootData.find("opa");
        if (it != _rootData.end())
        {
            if (filePtr->parseReal(opa, it->second) && filePtr->isMotion)
                opa = opa / 255;
        }
        // mesh
        auto it = _rootData.find("mesh");
        if (it != _rootData.end())
        {
            filePtr->parseObject(_rootData, it->second);
            // bp
            std::vector<uint32_t> meshOffset;
            filePtr->parseList(meshOffset, _rootData["bp"]);
            if (meshOffset.size() == 32)
            {
                hasbp = true;
                for (int i = 0; i < 32; i++)
                    filePtr->parseReal(bp[i], meshOffset[i]);
            }
            // cc
            auto it = _rootData.find("cc");
            if (it != _rootData.end())
            {
                if (filePtr->parseObject(_rootData, it->second))
                {
                    hascc = true;
                    // c
                    meshOffset.clear();
                    filePtr->parseList(meshOffset, _rootData["c"]);
                    if (meshOffset.size() == 2)
                    {
                        for (int i = 0; i < 2; i++)
                            filePtr->parseReal(cc_c[i], meshOffset[i]);
                    }
                    // x
                    meshOffset.clear();
                    filePtr->parseList(meshOffset, _rootData["x"]);
                    if (meshOffset.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(cc_x[i], meshOffset[i]);
                    }
                    // y
                    meshOffset.clear();
                    filePtr->parseList(meshOffset, _rootData["y"]);
                    if (meshOffset.size() == 4)
                    {
                        for (int i = 0; i < 4; i++)
                            filePtr->parseReal(cc_y[i], meshOffset[i]);
                    }
                }
            }
        }
    }
}
emoteframe::~emoteframe()
{
}

emotenode::emotenode(emotemotion* rootmotion,
                     emotenode* parent,
                     std::vector<emotenode*>& nodeList,
                     emotefile* filePtr,
                     uint32_t startOffset)
  : _parent(parent),
    _rootmotion(rootmotion),
    _filePtr(filePtr)
{
    // add to mgn ss
    nodeList.push_back(this);
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // meshInfo
    int64_t tmp = 0;
    auto it = _rootData.find("meshCombine");
    if (it != _rootData.end())
    {
        filePtr->parseNumber(tmp, it->second);
        meshCombine = static_cast<uint8_t>(tmp);
    }
    it = _rootData.find("meshDivision");
    if (it != _rootData.end())
    {
        filePtr->parseNumber(tmp, it->second);
        meshDivision = static_cast<uint8_t>(tmp);
    }
    it = _rootData.find("meshTransform");
    if (it != _rootData.end())
    {
        filePtr->parseNumber(tmp, it->second);
        meshTransform = static_cast<uint8_t>(tmp);
    }
    filePtr->parseNumber(tmp, _rootData["type"]);
    type = static_cast<uint8_t>(tmp);
    // mask
    it = _rootData.find("meshSyncChildMask");
    if (it != _rootData.end())
    {
        filePtr->parseNumber(tmp, it->second);
        meshSyncChildMask = static_cast<uint32_t>(tmp);
    }
    it = _rootData.find("inheritMask");
    if (it != _rootData.end())
    {
        filePtr->parseNumber(tmp, it->second);
        inheritMask = static_cast<uint32_t>(tmp);
    }
    // var
    it = _rootData.find("parameterize");
    if (it != _rootData.end())
    {
        if (filePtr->parseNumber(tmp, it->second))
        {
            isParameterize = true;
            parameterIdx = static_cast<int32_t>(tmp);
        }
    }
    // label
    filePtr->parseString(label, _rootData["label"]);
    // frameList
    std::vector<uint32_t> _tmpList;
    filePtr->parseList(_tmpList, _rootData["frameList"]);
    for (size_t i = 0; i < _tmpList.size(); i++)
    {
        emoteframe* tmp = new emoteframe(filePtr, _tmpList.at(i));
        frameList.push_back(tmp);
    }
    // children
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["children"]);
    for (size_t i = 0; i < _tmpList.size(); i++)
    {
        emotenode* tmp = new emotenode(_rootmotion, this, nodeList, filePtr, _tmpList.at(i));
        children.push_back(tmp);
    }
    // refmask List
    _tmpList.clear();
    it = _rootData.find("stencilCompositeMaskLayerList");
    if (it != _rootData.end())
    {
        filePtr->parseList(_tmpList, it->second);
        for (size_t i = 0; i < _tmpList.size(); i++)
        {
            std::string refName;
            filePtr->parseString(refName, _tmpList.at(i));
            stencilCompositeMaskLayerList.push_back(refName);
        }
    }
}
emotenode::~emotenode()
{
    for (auto frame : frameList)
    {
        if (frame != nullptr)
            delete frame;
    }
    for (auto child : children)
    {
        if (child != nullptr)
            delete child;
    }
}

emotemotion::emotemotion(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // time
    filePtr->parseReal(lastTime, _rootData["lastTime"]);
    filePtr->parseReal(loopTime, _rootData["loopTime"]);
    // layer
    std::vector<uint32_t> _layer;
    filePtr->parseList(_layer, _rootData["layer"]);
    for (size_t i = 0; i < _layer.size(); i++)
    {
        emotenode* tmp = new emotenode(this, nullptr, nodeList, filePtr, _layer.at(i));
        layer.push_back(tmp);
    }

    // priority
    _layer.clear();
    filePtr->parseList(_layer, _rootData["priority"]);
    if (_layer.size() > 0)
    {
        std::map<std::string, uint32_t> _priority;
        filePtr->parseObject(_priority, _layer.at(0));
        auto it = _priority.find("content");
        if (it != _priority.end())
        {
            _layer.clear();
            filePtr->parseList(_layer, _priority["content"]);
            for (auto idx : _layer)
            {
                int64_t tmp;
                filePtr->parseNumber(tmp, idx);
                priority.push_back(static_cast<int32_t>(tmp));
            }
        }
    }
    // rearrange
    std::vector<emotenode*> result;
    for (auto node_idx = priority.begin(); node_idx != priority.end(); ++node_idx)
    {
        if (*node_idx >= nodeList.size())
            continue;

        result.push_back(nodeList.at(*node_idx));
    }
    nodeList = result;

    // parameter
    _layer.clear();
    filePtr->parseList(_layer, _rootData["parameter"]);
    for (auto varItem : _layer)
    {
        std::map<std::string, uint32_t> varMap;
        filePtr->parseObject(varMap, varItem);
        emoteVar* var = new emoteVar();
        int64_t tmp = 0;
        filePtr->parseNumber(tmp, varMap["rangeBegin"]);
        var->rangeBegin = static_cast<int32_t>(tmp);
        filePtr->parseNumber(tmp, varMap["rangeEnd"]);
        var->rangeEnd = static_cast<int32_t>(tmp);
        filePtr->parseNumber(tmp, varMap["division"]);
        var->division = static_cast<int32_t>(tmp);
        filePtr->parseString(var->id, varMap["id"]);
        parameter.push_back(var);
    }
    // parameter to child
    auto it = _rootData.find("parameterize");
    if (it != _rootData.end())
    {
        int64_t tmp = 0;
        if (filePtr->parseNumber(tmp, it->second))
        {
            isParameterize = true;
            parameterIdx = static_cast<int32_t>(tmp);
        }
        if (filePtr->isMotion)
        {
            std::map<std::string, uint32_t> varMap;
            if (filePtr->parseObject(varMap, it->second))
            {
                isParameterize = true;
                parameterIdx = 0;
                emoteVar* var = new emoteVar();
                int64_t tmp = 0;
                filePtr->parseNumber(tmp, varMap["rangeBegin"]);
                var->rangeBegin = static_cast<int32_t>(tmp);
                filePtr->parseNumber(tmp, varMap["rangeEnd"]);
                var->rangeEnd = static_cast<int32_t>(tmp);
                filePtr->parseNumber(tmp, varMap["discretization"]);
                var->division = static_cast<int32_t>(tmp);
                filePtr->parseString(var->id, varMap["id"]);
                parameter.push_back(var);
            }
        }
    }
    if (isParameterize)
    {
        for (auto itmNode : nodeList)
        {
            if (!itmNode->isParameterize)
            {
                itmNode->isParameterize = true;
                itmNode->parameterIdx = parameterIdx;
            }
        }
    }

    // selfSyncTime
    if (filePtr->isMotion && !isParameterize)
    {
        for (auto _node : nodeList)
        {
            for (auto _frame : _node->frameList)
            {
                if (_frame != nullptr && _frame->hasContent)
                {
                    if (_frame->time > selfSyncTime)
                        selfSyncTime = _frame->time;
                }
            }
        }
    }
}
emotemotion::~emotemotion()
{
    for (auto lay : layer)
    {
        if (lay != nullptr)
            delete lay;
    }
    for (auto para : parameter)
    {
        if (para != nullptr)
            delete para;
    }
}
emotenode* emotemotion::getNodeByName(const std::string& name)
{
    for (auto node : nodeList)
    {
        if (node == nullptr)
            continue;
        if (strcmp(name.c_str(), node->label.c_str()) == 0) // 仅靠一个label，合理吗？
        {
            return node;
        }
    }
    return nullptr;
}

emoteobject::emoteobject(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // type
    int64_t tmp;
    filePtr->parseNumber(tmp, _rootData["type"]);
    type = static_cast<int8_t>(tmp);
    // motion
    std::map<std::string, uint32_t> _motion;
    filePtr->parseObject(_motion, _rootData["motion"]);
    for (auto _obj : _motion)
    {
        emotemotion* tmp = new emotemotion(filePtr, _obj.second);
        tmp->parent = this;
        tmp->name = _obj.first;
        motion.insert(std::pair<std::string, emotemotion*>(_obj.first, tmp));
    }
}
emoteobject::~emoteobject()
{
    for (auto mtn : motion)
    {
        if (mtn.second != nullptr)
            delete mtn.second;
    }
}
emoteVar* emoteobject::findVarByName(const std::string& name)
{
    for (auto mtn : motion)
    {
        for (auto var : mtn.second->parameter)
        {
            if (var->id == name)
            {
                return var;
            }
        }
    }
    return nullptr;
}

#pragma endregion

#pragma region timeline

emoteTimeVarFrame::emoteTimeVarFrame(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // time type
    filePtr->parseReal(time, _rootData["time"]);
    int64_t tmp;
    filePtr->parseNumber(tmp, _rootData["type"]);
    type = static_cast<int8_t>(tmp);
    // content
    auto it = _rootData.find("content");
    if (it != _rootData.end())
    {
        if (filePtr->parseObject(_rootData, it->second))
        {
            hasContent = true;
            // easing value
            it = _rootData.find("easing");
            if (it != _rootData.end())
            {
                filePtr->parseReal(easing, it->second);
            }
            it = _rootData.find("value");
            if (it != _rootData.end())
            {
                filePtr->parseReal(value, it->second);
            }
        }
    }
}
emoteTimeVarFrame::~emoteTimeVarFrame()
{
}
emoteTimeVar::emoteTimeVar(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // label
    filePtr->parseString(label, _rootData["label"]);
    // frameList
    std::vector<uint32_t> _tmpList;
    filePtr->parseList(_tmpList, _rootData["frameList"]);
    for (size_t i = 0; i < _tmpList.size(); i++)
    {
        emoteTimeVarFrame* tmp = new emoteTimeVarFrame(filePtr, _tmpList.at(i));
        frameList.push_back(tmp);
    }
}
emoteTimeVar::~emoteTimeVar()
{
    for (auto itm : frameList)
    {
        if (itm != nullptr)
            delete itm;
    }
}
emotetimeline::emotetimeline(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // frameinfo
    int64_t tmp = 0;
    filePtr->parseNumber(tmp, _rootData["lastTime"]);
    lastTime = static_cast<int32_t>(tmp);
    filePtr->parseNumber(tmp, _rootData["loopBegin"]);
    loopBegin = static_cast<int32_t>(tmp);
    filePtr->parseNumber(tmp, _rootData["loopEnd"]);
    loopEnd = static_cast<int32_t>(tmp);
    if (filePtr->parseNumber(tmp, _rootData["diff"]))
        diff = static_cast<int8_t>(tmp);

    // label
    filePtr->parseString(label, _rootData["label"]);
    // variableList
    std::vector<uint32_t> _tmpList;
    filePtr->parseList(_tmpList, _rootData["variableList"]);
    for (size_t i = 0; i < _tmpList.size(); i++)
    {
        emoteTimeVar* tmp = new emoteTimeVar(filePtr, _tmpList.at(i));
        variableList.push_back(tmp);
    }
}
emotetimeline::~emotetimeline()
{
    for (auto itm : variableList)
    {
        if (itm != nullptr)
            delete itm;
    }
}

#pragma endregion

#pragma region Physics

bustControl::bustControl(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // friction/gravity/spring
    filePtr->parseReal(friction, _rootData["friction"]);
    filePtr->parseReal(gravity, _rootData["gravity"]);
    filePtr->parseReal(spring, _rootData["spring"]);
    // scale_x/y
    filePtr->parseReal(scale_x, _rootData["scale_x"]);
    filePtr->parseReal(scale_y, _rootData["scale_y"]);
    // lable
    filePtr->parseString(var_lr, _rootData["var_lr"]);
    filePtr->parseString(var_ud, _rootData["var_ud"]);
    // param
    if (filePtr->parseObject(_rootData, _rootData["param"]))
    {
        filePtr->parseReal(param.ofs, _rootData["ofs"]);
        // op
        std::map<std::string, uint32_t> _tmpData;
        if (filePtr->parseObject(_tmpData, _rootData["op"]))
        {
            filePtr->parseReal(param.op.x, _tmpData["x"]);
            filePtr->parseReal(param.op.y, _tmpData["y"]);
            filePtr->parseReal(param.op.z, _tmpData["z"]);
        }
        // p
        if (filePtr->parseObject(_tmpData, _rootData["p"]))
        {
            filePtr->parseReal(param.p.x, _tmpData["x"]);
            filePtr->parseReal(param.p.y, _tmpData["y"]);
            filePtr->parseReal(param.p.z, _tmpData["z"]);
        }
        // pv
        if (filePtr->parseObject(_tmpData, _rootData["pv"]))
        {
            filePtr->parseReal(param.pv.x, _tmpData["x"]);
            filePtr->parseReal(param.pv.y, _tmpData["y"]);
            filePtr->parseReal(param.pv.z, _tmpData["z"]);
        }
    }
}
bustControl::~bustControl()
{
}

uniControl::uniControl(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // b_rate/bend_spd/bend_vol/friction_x/friction_y/gravity
    filePtr->parseReal(b_rate, _rootData["b_rate"]);
    filePtr->parseReal(bend_spd, _rootData["bend_spd"]);
    filePtr->parseReal(bend_vol, _rootData["bend_vol"]);
    filePtr->parseReal(friction_x, _rootData["friction_x"]);
    filePtr->parseReal(friction_y, _rootData["friction_y"]);
    filePtr->parseReal(gravity, _rootData["gravity"]);
    // ud_eft/v_bound
    filePtr->parseReal(ud_eft, _rootData["ud_eft"]);
    filePtr->parseReal(v_bound, _rootData["v_bound"]);
    // lable
    filePtr->parseString(var_lr, _rootData["var_lr"]);
    filePtr->parseString(var_lrm, _rootData["var_lrm"]);
    filePtr->parseString(var_ud, _rootData["var_ud"]);
    // param
    std::vector<uint32_t> _sx, _sy, _len;
    filePtr->parseList(_sx, _rootData["scale_x"]);
    filePtr->parseList(_sy, _rootData["scale_y"]);
    filePtr->parseList(_len, _rootData["length"]);
    if (filePtr->parseObject(_rootData, _rootData["param"]))
    {
        // ofs/bendR/bendS/op
        filePtr->parseReal(ofs, _rootData["ofs"]);
        filePtr->parseReal(bendR, _rootData["bendR"]);
        filePtr->parseReal(bendS, _rootData["bendS"]);
        std::map<std::string, uint32_t> _tmpData;
        if (filePtr->parseObject(_tmpData, _rootData["op"]))
        {
            filePtr->parseReal(op.x, _tmpData["x"]);
            filePtr->parseReal(op.y, _tmpData["y"]);
            filePtr->parseReal(op.z, _tmpData["z"]);
        }
        // for get
        std::vector<uint32_t> _bp, _p, _pv;
        filePtr->parseList(_bp, _rootData["bp"]);
        filePtr->parseList(_p, _rootData["p"]);
        filePtr->parseList(_pv, _rootData["pv"]);
        // bp/p/pv
        int cts = std::min(_bp.size(), std::min(_p.size(), _pv.size()));
        for (int i = 0; i < cts; i++)
        {
            uniSegment _psD;
            filePtr->parseObject(_tmpData, _bp.at(i));
            filePtr->parseReal(_psD.bp.x, _tmpData["x"]);
            filePtr->parseReal(_psD.bp.y, _tmpData["y"]);
            filePtr->parseReal(_psD.bp.z, _tmpData["z"]);
            filePtr->parseObject(_tmpData, _p.at(i));
            filePtr->parseReal(_psD.p.x, _tmpData["x"]);
            filePtr->parseReal(_psD.p.y, _tmpData["y"]);
            filePtr->parseReal(_psD.p.z, _tmpData["z"]);
            filePtr->parseObject(_tmpData, _pv.at(i));
            filePtr->parseReal(_psD.pv.x, _tmpData["x"]);
            filePtr->parseReal(_psD.pv.y, _tmpData["y"]);
            filePtr->parseReal(_psD.pv.z, _tmpData["z"]);
            filePtr->parseReal(_psD.scale_x, _sx.at(i));
            filePtr->parseReal(_psD.scale_y, _sy.at(i));
            filePtr->parseReal(_psD.length, _len.at(i));
            segments.push_back(_psD);
        }
    }
}
uniControl::~uniControl()
{
}

#pragma endregion

emoteattrcomp::emoteattrcomp(emotefile* filePtr, uint32_t startOffset)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // lable
    filePtr->parseString(lable, _rootData["label"]);
    // remove_data
    if (filePtr->parseObject(_rootData, _rootData["data"]))
    {
        std::vector<uint32_t> _tmpVec;
        if (filePtr->parseList(_tmpVec, _rootData["remove"]))
        {
            for (auto vecItm : _tmpVec)
            {
                emoteattrcompRemoveItem _tmpitm;
                if (filePtr->parseObject(_rootData, vecItm))
                {
                    filePtr->parseReal(_tmpitm.value, _rootData["value"]);
                    if (filePtr->parseObject(_rootData, _rootData["id"]))
                    {
                        filePtr->parseString(_tmpitm.chara, _rootData["chara"]);
                        filePtr->parseString(_tmpitm.motion, _rootData["motion"]);
                        filePtr->parseString(_tmpitm.layer, _rootData["layer"]);
                    }
                }
                data_remove.push_back(_tmpitm);
            }
        }
    }
}
emoteattrcomp::~emoteattrcomp()
{
}

emoteselect::emoteselect(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // lable
    filePtr->parseString(lable, _rootData["label"]);
    // optionList
    std::vector<uint32_t> _tmpList;
    if (filePtr->parseList(_tmpList, _rootData["optionList"]))
    {
        for (auto attcom : _tmpList)
        {
            if (filePtr->parseObject(_rootData, attcom))
            {
                emoteselectItem _tmpItm;
                filePtr->parseString(_tmpItm.label, _rootData["label"]);
                filePtr->parseReal(_tmpItm.offValue, _rootData["offValue"]);
                filePtr->parseReal(_tmpItm.onValue, _rootData["onValue"]);
                selectItem.push_back(_tmpItm);
            }
        }
    }
}
emoteselect::~emoteselect()
{
}
bool emoteselect::selectValue(int32_t opt)
{
    if (opt < 0 || opt >= selectItem.size())
        return false;
    for (int i = 0; i < selectItem.size(); i++)
    {
        if (i == opt)
            _filePtr->setVariable(selectItem.at(i).label, selectItem.at(i).onValue);
        else
            _filePtr->setVariable(selectItem.at(i).label, selectItem.at(i).offValue);
    }
    return true;
}

emotemetadata::emotemetadata(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    if (!filePtr->parseObject(_rootData, startOffset))
    {
        filePtr->isMotion = true;
        return;
    }
    // label
    std::map<std::string, uint32_t> _base;
    filePtr->parseObject(_base, _rootData["base"]);
    filePtr->parseString(chara, _base["chara"]);
    filePtr->parseString(motion, _base["motion"]);

    // attrcomp
    std::vector<uint32_t> _tmpList;
    auto attrItm = _rootData.find("attrcomp");
    if (attrItm != _rootData.end())
    {
        if (filePtr->parseList(_tmpList, attrItm->second))
        {
            for (auto attcom : _tmpList)
            {
                emoteattrcomp* _tmpattr = new emoteattrcomp(filePtr, attcom);
                _attrcomp.push_back(_tmpattr);
            }
        }
    }
    // selectorControl
    _tmpList.clear();
    attrItm = _rootData.find("selectorControl");
    if (attrItm != _rootData.end())
    {
        if (filePtr->parseList(_tmpList, attrItm->second))
        {
            for (auto attcom : _tmpList)
            {
                emoteselect* _tmpattr = new emoteselect(filePtr, attcom);
                _selectorControl.push_back(_tmpattr);
            }
        }
    }
    // mirrorControl
    attrItm = _rootData.find("mirror");
    if (attrItm != _rootData.end())
    {
        tjs_int64 isNeed = 0;
        filePtr->parseNumber(isNeed, attrItm->second);
        if (isNeed == 1)
        {
            filePtr->isMirror = true;
            // if (filePtr->parseObject(_base, _rootData["mirrorControl"]))
            //{
            //     auto it = _base.find("variableMatchList");
            //     if (it != _base.end())
            //     {
            //         std::vector<uint32_t> tmpList;
            //         if (filePtr->parseList(tmpList, it->second))
            //         {
            //             for (auto mtc : tmpList)
            //             {
            //                 std::string tmpStr;
            //                 filePtr->parseString(tmpStr, mtc);
            //                 _mirrorControl.push_back(tmpStr);
            //             }
            //         }
            //     }
            // }
        }
    }

    // timelineControl
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["timelineControl"]);
    for (auto itm : _tmpList)
    {
        emotetimeline* tmp = new emotetimeline(filePtr, itm);
        _timelineControl.push_back(tmp);
    }

    // variableList Simplified
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["variableList"]);
    for (auto itm : _tmpList)
    {
        filePtr->parseObject(_base, itm);
        auto it = _base.find("label");
        if (it != _base.end())
        {
            std::string key;
            filePtr->parseString(key, it->second);
            _varList.insert(std::pair<std::string, float>(key, 0.0f));
        }
    }

    // eyeControl Simplified
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["eyeControl"]);
    for (auto itm : _tmpList)
    {
        filePtr->parseObject(_base, itm);
        eyeControl* tmp = new eyeControl();
        int64_t tmpNum = 0;
        auto it = _base.find("label");
        if (it != _base.end())
        {
            filePtr->parseString(tmp->label, it->second);
        }
        it = _base.find("beginFrame");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->beginFrame = static_cast<float>(tmpNum);
        }
        it = _base.find("endFrame");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->endFrame = static_cast<float>(tmpNum);
        }
        else
        {
            tmp->endFrame = tmp->beginFrame;
        }
        it = _base.find("blinkFrameCount");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->blinkFrameCount = static_cast<float>(tmpNum);
        }
        it = _base.find("blinkIntervalMax");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->blinkIntervalMax = static_cast<float>(tmpNum);
        }
        it = _base.find("blinkIntervalMin");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->blinkIntervalMin = static_cast<float>(tmpNum);
        }
        tmp->uid =
            std::uniform_int_distribution<int32_t>(tmp->blinkIntervalMin, tmp->blinkIntervalMax);
        _eyeControl.push_back(tmp);
    }
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["eyebrowControl"]);
    for (auto itm : _tmpList)
    {
        filePtr->parseObject(_base, itm);
        eyebrowControl* tmp = new eyebrowControl();
        int64_t tmpNum = 0;
        auto it = _base.find("label");
        if (it != _base.end())
        {
            filePtr->parseString(tmp->label, it->second);
        }
        it = _base.find("beginFrame");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->beginFrame = static_cast<float>(tmpNum);
        }
        it = _base.find("endFrame");
        if (it != _base.end())
        {
            filePtr->parseNumber(tmpNum, it->second);
            tmp->endFrame = static_cast<float>(tmpNum);
        }
        else
        {
            tmp->endFrame = tmp->beginFrame;
        }
        _eyebrowControl.push_back(tmp);
    }

    // bustControl
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["bustControl"]);
    for (auto itm : _tmpList)
    {
        bustControl* tmp = new bustControl(filePtr, itm);
        _bustControl.push_back(tmp);
    }
    // hairControl
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["hairControl"]);
    for (auto itm : _tmpList)
    {
        uniControl* tmp = new uniControl(filePtr, itm);
        _hairControl.push_back(tmp);
    }
    // partsControl
    _tmpList.clear();
    filePtr->parseList(_tmpList, _rootData["partsControl"]);
    for (auto itm : _tmpList)
    {
        uniControl* tmp = new uniControl(filePtr, itm);
        _partsControl.push_back(tmp);
    }
}
emotemetadata::~emotemetadata()
{
    for (auto itm : _attrcomp)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _selectorControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _timelineControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _eyeControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _eyebrowControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _bustControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _hairControl)
    {
        if (itm != nullptr)
            delete itm;
    }
    for (auto itm : _partsControl)
    {
        if (itm != nullptr)
            delete itm;
    }
}

emoteicon::emoteicon(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // base
    filePtr->parseReal(originX, _rootData["originX"]);
    filePtr->parseReal(originY, _rootData["originY"]);
    filePtr->parseReal(width, _rootData["width"]);
    filePtr->parseReal(height, _rootData["height"]);
    if (filePtr->isKrkr)
    {
        // compress type
        auto it = _rootData.find("compress");
        if (it == _rootData.end())
        {
            compress = "none";
        }
        else
        {
            filePtr->parseString(compress, it->second);
        }
        // pixel id
        filePtr->parseResource(pixel, _rootData["pixel"]);
        // is pal?
        it = _rootData.find("pal");
        if (it != _rootData.end())
        {
            type = "pal";
            filePtr->parseResource(pal, it->second);
        }
    }
    else
    {
        // left/top
        filePtr->parseReal(left, _rootData["left"]);
        filePtr->parseReal(top, _rootData["top"]);
    }

    // clip
    auto it = _rootData.find("clip");
    if (it != _rootData.end())
    {
        filePtr->parseObject(_rootData, it->second);
        filePtr->parseReal(_clip.bottom, _rootData["bottom"]);
        filePtr->parseReal(_clip.left, _rootData["left"]);
        filePtr->parseReal(_clip.right, _rootData["right"]);
        filePtr->parseReal(_clip.top, _rootData["top"]);
    }
}
emoteicon::~emoteicon()
{
    if (data != nullptr)
        delete[] data;
    // 纹理由 core/render 的 2D 渲染器统一管理
    if (selftexture != nullptr)
    {
        krkrsdl3::iTVPRenderBackend* renderer = krkrsdl3::TVPGetRenderBackend();
        if (renderer)
            renderer->DestroyTexture(selftexture);
        selftexture = nullptr;
    }
}
void emoteicon::ensureLoad()
{
    if (data == nullptr)
    {
        // 基本创建
        int datasize = width * height * 4;
        data = new uint8_t[datasize];
        std::memset(data, 0, datasize);
        // 读取像素数据
        _filePtr->readIconTobuffer(data, width * height * 4, width * 4, this);

        // 统一字节序为 RGBA（原 GL 桌面路径以 BGRA 直传、软渲染路径做 R/B 交换，
        // 抽象后所有后端统一，行为一致）
        if (_filePtr->colorType == 0)
        {
            for (size_t i = 0; i < (size_t)width * height; i++)
            {
                uint8_t tmp = data[4 * i];
                data[4 * i] = data[4 * i + 2];
                data[4 * i + 2] = tmp;
            }
        }
        else if (_filePtr->colorType != 1)
        {
            TVPConsoleLog("unknow colorType");
        }

        // 纹理交给 core/render 的 2D 渲染器（GL 后端建 GPU 纹理，软渲染后端建 CPU 缓冲）
        krkrsdl3::iTVPRenderBackend* renderer = krkrsdl3::TVPGetRenderBackend();
        if (renderer)
        {
            selftexture = renderer->CreateTexture(width, height);
            if (selftexture)
                renderer->UpdateTexture(selftexture, data, width, height, width * 4);
        }
    }
}

emotesource::emotesource(emotefile* filePtr, uint32_t startOffset) : _filePtr(filePtr)
{
    // dic
    std::map<std::string, uint32_t> _rootData;
    filePtr->parseObject(_rootData, startOffset);
    // type
    int64_t tmp;
    filePtr->parseNumber(tmp, _rootData["type"]);
    type = static_cast<int8_t>(tmp);
    // icon
    std::map<std::string, uint32_t> _motion;
    filePtr->parseObject(_motion, _rootData["icon"]);
    for (auto _obj : _motion)
    {
        emoteicon* tmp = new emoteicon(filePtr, _obj.second);
        icon.insert(std::pair<std::string, emoteicon*>(_obj.first, tmp));
    }
    // texture
    if (!filePtr->isKrkr)
    {
        if (filePtr->parseObject(_rootData, _rootData["texture"]))
        {
            std::string intType;
            double width = 0;
            double height = 0;
            int32_t pixel = -1;
            filePtr->parseReal(width, _rootData["width"]);
            filePtr->parseReal(height, _rootData["height"]);
            filePtr->parseString(intType, _rootData["type"]);
            filePtr->parseResource(pixel, _rootData["pixel"]);
            // 子类赋值
            for (auto _obj : icon)
            {
                _obj.second->type = intType;
                _obj.second->pixel = pixel;
                _obj.second->texWidth = width;
                _obj.second->texHeight = height;
            }
        }
    }
}
emotesource::~emotesource()
{
    for (auto ic : icon)
    {
        if (ic.second != nullptr)
            delete ic.second;
    }
}

emotefile::emotefile()
{
    _header.version = 0;
    _header.encrypt = 0;
    _header.offsetEncrypt = 0;
    _header.offsetNames = 0;
    _header.offsetStrings = 0;
    _header.offsetStringsData = 0;
    _header.offsetChunkOffsets = 0;
    _header.offsetChunkLengths = 0;
    _header.offsetChunkData = 0;
    _header.offsetEntries = 0;
    _header.checksum = 0;
    _header.offsetExtraChunkOffsets = 0;
    _header.offsetExtraChunkLengths = 0;
    _header.offsetExtraChunkData = 0;
}
emotefile::~emotefile()
{
    if (filePtr != nullptr)
        delete filePtr;
    ClearAniTree();
}
void emotefile::setSeed(tjs_int seed)
{
    _seed = seed;
}
void emotefile::setFun(tTJSVariantClosure decryptClo)
{
    _decryptClo = decryptClo;
}
bool emotefile::load(const ttstr& filePath)
{
    if (filePtr != NULL)
        delete filePtr;
    filePtr = TVPCreateStream(filePath);
    if (!filePtr)
        return false;
    const size_t readSize = filePtr->GetSize();
    if (readSize < 9)
        return false;

    // Decompress MDF
    char sign[5];
    filePtr->Read(sign, 5);
    sign[4] = '\0';
    char lzfs[] = {0x04, 0x22, 0x4d, 0x18, 0x00};
    if (TJS_strcasecmp(sign, lzfs) == 0) // lzfs
    {
        filePtr = Lz4Stream::GetLz4Stream(filePtr);
    }
    else
    {
        sign[3] = '\0';
        if (TJS_strcasecmp(sign, "MDF") == 0)
        {
            // uncompress data
            uLongf uncompressedSize = filePtr->ReadI32LE();
            tjs_uint8* _uncompress_buffer = new tjs_uint8[uncompressedSize];
            // compress data
            tjs_uint8* _compress_buffer = new tjs_uint8[readSize - 8];
            filePtr->Read(_compress_buffer, readSize - 8);
            // uncompress buffer
            if (uncompress(_uncompress_buffer, &uncompressedSize, _compress_buffer, readSize - 8) !=
                Z_OK)
            {
                delete[] _uncompress_buffer;
                delete[] _compress_buffer;
                return false;
            }
            // compress data
            tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, uncompressedSize);
            memcpy(_stream->GetInternalBuffer(), _uncompress_buffer, uncompressedSize);
            // clear data
            delete[] _uncompress_buffer;
            delete[] _compress_buffer;
            delete filePtr;
            filePtr = _stream;
        }
        else // 使用memory防止xp3filter解密所带来的性能下降，不过内存可能得遭罪了
        {
            tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
            filePtr->SetPosition(0);
            filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());
            delete filePtr;
            filePtr = _stream;
        }
    }

    // ReadHeader
    filePtr->SetPosition(0);
    _header.parsePSBHeader(filePtr);

    if (_seed > 0)
    {
        // decrypt
        tjs_uint32 key[4];
        key[0] = 0x075BCD15;
        key[1] = 0x159A55E5;
        key[2] = 0x1F123BB5;
        key[3] = _seed;
        EMoteCTX emoteCtx{};
        init_emote_ctx(&emoteCtx, key);

        if (_header.isEncrypted() && _header.getHeaderLength() > filePtr->GetSize())
        {

            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetEncrypt), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetNames), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetStrings), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetStringsData), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkOffsets), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkLengths), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetChunkData), 4);
            emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.offsetEntries), 4);

            if (_header.version > 2)
            {
                emote_decrypt(&emoteCtx, reinterpret_cast<tjs_uint8*>(&_header.checksum), 4);
            }

            if (_header.version > 3)
            {
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkOffsets), 4);
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkLengths), 4);
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<tjs_uint8*>(&_header.offsetExtraChunkData), 4);
            }
        }

        if (_header.version == 2)
        {
            // convert to memorystream
            tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
            filePtr->SetPosition(0);
            filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());

            emote_decrypt(
                &emoteCtx,
                &static_cast<std::uint8_t*>(_stream->GetInternalBuffer())[_header.offsetEncrypt],
                _header.offsetChunkOffsets - _header.offsetEncrypt);

            delete filePtr;
            filePtr = _stream;
        }
    }
    if (_decryptClo.Object != NULL)
    {
        tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
        filePtr->SetPosition(0);
        filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());
        CBinaryAccessor* cba =
            new CBinaryAccessor((unsigned char*)_stream->GetInternalBuffer(), _stream->GetSize());
        tTJSVariant buff(cba);
        cba->Release();
        tTJSVariant bufferLen((tjs_int)_stream->GetSize());
        tTJSVariant* vars[] = {&buff, &bufferLen};
        _decryptClo.FuncCall(0, NULL, NULL, NULL, 2, vars, NULL);

        delete filePtr;
        filePtr = _stream;

        // reReadHeader
        filePtr->SetPosition(0);
        _header.parsePSBHeader(filePtr);
    }

    // Encrypted
    if (_header.isEncrypted() && _header.getHeaderLength() > filePtr->GetSize())
    {
        TVPConsoleLog("psb file is encrypted");
        return false;
    }

    // Pre Load Strings
    filePtr->SetPosition(_header.offsetStrings);
    PSB::parsePSBArray(&stringsOffset,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);

    // Pre Load Names
    if (_header.version == 1)
    {
        // don't believe HeaderLength
        if (_header.offsetEncrypt >= filePtr->GetSize())
        {
            _header.offsetEncrypt = _header.getHeaderLength();
        }
        filePtr->SetPosition(_header.offsetEncrypt);
        PSB::parsePSBArray(
            &nameIndexes, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        // Load Names
        namesCache.reserve(nameIndexes.size());
        for (int i = 0; i < namesCache.size(); i++)
        {
            filePtr->SetPosition(_header.offsetNames + nameIndexes[i]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    break;
                }
                str.append(1, val8);
            }
            namesCache.push_back(str);
        }
    }
    else
    {
        filePtr->SetPosition(_header.offsetNames);
        PSB::parsePSBArray(
            &charset, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        PSB::parsePSBArray(
            &namesData, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        PSB::parsePSBArray(
            &nameIndexes, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        // Load Names
        namesCache.reserve(nameIndexes.size());
        for (int i = 0; i < nameIndexes.size(); i++)
        {
            std::string list;
            const auto index = nameIndexes[i];
            auto chr = namesData[index];
            while (chr != 0)
            {
                const auto code = namesData[chr];
                const auto d = charset[code];
                const auto realChr = chr - d;
                chr = code;
                list.append(1, realChr);
            }
            std::reverse(list.begin(), list.end());
            namesCache.push_back(list);
        }
    }

    // Pre Load Resources (Chunks)
    filePtr->SetPosition(_header.offsetChunkOffsets);
    PSB::parsePSBArray(&chunkOffsets,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);
    filePtr->SetPosition(_header.offsetChunkLengths);
    PSB::parsePSBArray(&chunkLengths,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);

    if (_header.version >= 4 && _header.offsetExtraChunkOffsets < filePtr->GetSize())
    {
        // Pre Load Extra Resources (Chunks)
        filePtr->SetPosition(_header.offsetExtraChunkOffsets);
        PSB::parsePSBArray(
            &extraChunkOffsets,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
        filePtr->SetPosition(_header.offsetExtraChunkLengths);
        PSB::parsePSBArray(
            &extraChunkLengths,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
    }

    return GenerateAniTree();
}
tTJSVariant emotefile::root()
{
    return readAllObjs("root", _header.offsetEntries);
}
tTJSVariant emotefile::readAllObjs(const ttstr& key, tjs_uint32 _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::None:
            return tTJSVariant();
        case PSB::PSBObjType::Null:
            return tTJSVariant();
        case PSB::PSBObjType::False:
        case PSB::PSBObjType::True:
            return tTJSVariant(type == PSB::PSBObjType::True);
        case PSB::PSBObjType::NumberN0:
            return tTJSVariant(0);
        case PSB::PSBObjType::NumberN1:
        {
            tjs_int8 val8 = 0;
            filePtr->ReadBuffer(&val8, 1);
            return tTJSVariant(val8);
        }
        case PSB::PSBObjType::NumberN2:
        {
            tjs_int16 val16 = 0;
            filePtr->ReadBuffer(&val16, 2);
            return tTJSVariant(val16);
        }
        case PSB::PSBObjType::NumberN3:
        {
            tjs_int32 val32 = 0;
            filePtr->ReadBuffer(&val32, 3);
            val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
            return tTJSVariant(val32);
        }
        case PSB::PSBObjType::NumberN4:
        {
            tjs_int32 val32 = 0;
            filePtr->ReadBuffer(&val32, 4);
            return tTJSVariant(val32);
        }
        case PSB::PSBObjType::NumberN5:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 5);
            val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN6:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 6);
            val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN7:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 7);
            val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN8:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 8);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::Float0:
            return tTJSVariant(0.0f);
        case PSB::PSBObjType::Float:
        {
            tjs_uint8 buffer[4];
            filePtr->ReadBuffer(buffer, 4);
            float tmp;
            std::memcpy(&tmp, buffer, 4);
            return tTJSVariant((tjs_real)tmp);
        }
        case PSB::PSBObjType::Double:
        {
            tjs_uint8 buffer[8];
            filePtr->ReadBuffer(buffer, 8);
            double tmp;
            std::memcpy(&tmp, buffer, 8);
            return tTJSVariant((tjs_real)tmp);
        }
        case PSB::PSBObjType::ArrayN1:
        case PSB::PSBObjType::ArrayN2:
        case PSB::PSBObjType::ArrayN3:
        case PSB::PSBObjType::ArrayN4:
        case PSB::PSBObjType::ArrayN5:
        case PSB::PSBObjType::ArrayN6:
        case PSB::PSBObjType::ArrayN7:
        case PSB::PSBObjType::ArrayN8:
        {
            std::vector<tjs_uint32> tmp;
            PSB::parsePSBArray(&tmp, typeByte - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                               filePtr);
            iTJSDispatch2* array = TJSCreateArrayObject();
            for (auto i : tmp)
            {
                tTJSVariant tmp(static_cast<tjs_int32>(i));
                tTJSVariant* args[] = {&tmp};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
            tTJSVariant result(array, array);
            array->Release();
            return result;
        }
        case PSB::PSBObjType::StringN1:
        case PSB::PSBObjType::StringN2:
        case PSB::PSBObjType::StringN3:
        case PSB::PSBObjType::StringN4:
        {
            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx,
                                typeByte - static_cast<tjs_int8>(PSB::PSBObjType::StringN1) + 1);
            filePtr->SetPosition(_header.offsetStringsData + stringsOffset[idx]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    return tTJSVariant(str);
                }
                str.append(1, val8);
            }
            return tTJSVariant("");
        }
        case PSB::PSBObjType::ResourceN1:
        case PSB::PSBObjType::ResourceN2:
        case PSB::PSBObjType::ResourceN3:
        case PSB::PSBObjType::ResourceN4:
        {
            return tTJSVariant();
        }
        case PSB::PSBObjType::ExtraChunkN1:
        case PSB::PSBObjType::ExtraChunkN2:
        case PSB::PSBObjType::ExtraChunkN3:
        case PSB::PSBObjType::ExtraChunkN4:
        {
            return tTJSVariant();
        }
        case PSB::PSBObjType::List:
        {
            std::vector<tjs_uint32> _objsOffset;
            tjs_uint32 _tmpOffset = this->readListInfo(&_objsOffset);

            iTJSDispatch2* array = TJSCreateArrayObject();
            for (auto _offset : _objsOffset)
            {
                tTJSVariant obj = readAllObjs(ttstr(), _tmpOffset + _offset);
                tTJSVariant* args[] = {&obj};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
            tTJSVariant result(array, array);
            array->Release();
            return result;
        }
        case PSB::PSBObjType::Objects:
        {
            std::vector<tjs_uint32> _objsOffset, _objsNamesIdx;
            tjs_uint32 _tmpOffset = 0;
            if (_header.version == 1)
            {
                _tmpOffset = this->readListInfo(&_objsOffset);
                this->refreshListInfo(&_objsOffset, &_objsNamesIdx);
            }
            else
            {
                this->readListInfo(&_objsNamesIdx);
                _tmpOffset = this->readListInfo(&_objsOffset);
            }

            iTJSDispatch2* dsp = TJSCreateDictionaryObject();
            for (tjs_int i = 0; i < _objsNamesIdx.size(); i++)
            {
                ttstr keyName = namesCache.at(_objsNamesIdx.at(i));
                tTJSVariant obj = readAllObjs(keyName, _tmpOffset + _objsOffset.at(i));
                dsp->PropSet(TJS_MEMBERENSURE, keyName.c_str(), nullptr, &obj, dsp);
            }
            tTJSVariant result(dsp, dsp);
            dsp->Release();
            return result;
        }
        default:
            TVPConsoleLog("unknown psbObjType");
            return tTJSVariant();
    }
}
uint32_t emotefile::readListInfo(std::vector<uint32_t>* target)
{
    PSB::parsePSBArray(
        target, filePtr->ReadI8LE() - static_cast<int8_t>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
    return filePtr->GetPosition();
}
void emotefile::refreshListInfo(std::vector<uint32_t>* target1, std::vector<uint32_t>* target2)
{
    target2->reserve(target1->size());
    uint32_t basePose = filePtr->GetPosition();
    for (uint32_t i = 0; i < target1->size(); i++)
    {
        filePtr->SetPosition(basePose + target1->at(i));
        target2->at(i) = filePtr->ReadI8LE();
        target1->at(i) += 4;
    }
}
bool emotefile::parseObject(std::map<std::string, uint32_t>& output, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    if (static_cast<PSB::PSBObjType>(typeByte) == PSB::PSBObjType::Null)
    {
        return false;
    }
    else if (static_cast<PSB::PSBObjType>(typeByte) != PSB::PSBObjType::Objects)
    {
        TVPConsoleLog("Invalidate Object");
        return false;
    }
    output.clear();

    std::vector<uint32_t> _objsOffset, _objsNamesIdx;
    uint32_t _tmpOffset = 0;
    if (_header.version == 1)
    {
        _tmpOffset = this->readListInfo(&_objsOffset);
        this->refreshListInfo(&_objsOffset, &_objsNamesIdx);
    }
    else
    {
        this->readListInfo(&_objsNamesIdx);
        _tmpOffset = this->readListInfo(&_objsOffset);
    }

    for (int32_t i = 0; i < _objsNamesIdx.size(); i++)
    {
        output.insert(std::pair<std::string, uint32_t>(namesCache.at(_objsNamesIdx.at(i)),
                                                       _tmpOffset + _objsOffset.at(i)));
    }
    return true;
}
bool emotefile::parseNumber(int64_t& output, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::NumberN0:
        {
            output = 0;
            return true;
        }
        case PSB::PSBObjType::NumberN1:
        {
            int8_t val8 = 0;
            filePtr->ReadBuffer(&val8, 1);
            output = static_cast<int64_t>(val8);
            return true;
        }
        case PSB::PSBObjType::NumberN2:
        {
            int16_t val16 = 0;
            filePtr->ReadBuffer(&val16, 2);
            output = static_cast<int64_t>(val16);
            return true;
        }
        case PSB::PSBObjType::NumberN3:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 3);
            val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN4:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 4);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN5:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 5);
            val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN6:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 6);
            val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN7:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 7);
            val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN8:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 8);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::Null:
        {
            return false;
        }
        default:
            TVPConsoleLog("Invalidate Number");
            return false;
    }
}
bool emotefile::parseReal(double& output, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::NumberN0:
        {
            output = 0;
            return true;
        }
        case PSB::PSBObjType::NumberN1:
        {
            int8_t val8 = 0;
            filePtr->ReadBuffer(&val8, 1);
            output = static_cast<int64_t>(val8);
            return true;
        }
        case PSB::PSBObjType::NumberN2:
        {
            int16_t val16 = 0;
            filePtr->ReadBuffer(&val16, 2);
            output = static_cast<int64_t>(val16);
            return true;
        }
        case PSB::PSBObjType::NumberN3:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 3);
            val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN4:
        {
            int32_t val32 = 0;
            filePtr->ReadBuffer(&val32, 4);
            output = static_cast<int64_t>(val32);
            return true;
        }
        case PSB::PSBObjType::NumberN5:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 5);
            val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN6:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 6);
            val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN7:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 7);
            val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::NumberN8:
        {
            int64_t val64 = 0;
            filePtr->ReadBuffer(&val64, 8);
            output = static_cast<int64_t>(val64);
            return true;
        }
        case PSB::PSBObjType::Float0:
        {
            output = 0.0;
            return true;
        }
        case PSB::PSBObjType::Float:
        {
            tjs_uint8 buffer[4];
            filePtr->ReadBuffer(buffer, 4);
            float tmp;
            std::memcpy(&tmp, buffer, 4);
            output = static_cast<tjs_real>(tmp);
            return true;
        }
        case PSB::PSBObjType::Double:
        {
            tjs_uint8 buffer[8];
            filePtr->ReadBuffer(buffer, 8);
            double tmp;
            std::memcpy(&tmp, buffer, 8);
            output = static_cast<tjs_real>(tmp);
            return true;
        }
        case PSB::PSBObjType::Null:
        {
            return false;
        }
        default:
        {
            std::string retStr;
            if (parseString(retStr, _objOffset))
            {
                try
                {
                    output = std::stod(retStr);
                    return true;
                }
                catch (...)
                {
                    TVPConsoleLog("Invalidate Real");
                    return false;
                }
            }
            TVPConsoleLog("Invalidate Real");
            return false;
        }
    }
}
bool emotefile::parseString(std::string& output, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::StringN1:
        case PSB::PSBObjType::StringN2:
        case PSB::PSBObjType::StringN3:
        case PSB::PSBObjType::StringN4:
        {
            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx,
                                typeByte - static_cast<tjs_int8>(PSB::PSBObjType::StringN1) + 1);
            filePtr->SetPosition(_header.offsetStringsData + stringsOffset[idx]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    break;
                }
                str.append(1, val8);
            }
            output = str;
            return true;
        }
        default:
            TVPConsoleLog("Invalidate String");
            return false;
    }
}
bool emotefile::parseList(std::vector<uint32_t>& output, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    if ((static_cast<PSB::PSBObjType>(typeByte) == PSB::PSBObjType::Null))
    {
        return false;
    }
    else if (static_cast<PSB::PSBObjType>(typeByte) != PSB::PSBObjType::List)
    {
        TVPConsoleLog("Invalidate List");
        return false;
    }
    std::vector<uint32_t> _objsOffset;
    uint32_t _tmpOffset = this->readListInfo(&_objsOffset);

    for (auto _offset : _objsOffset)
    {
        output.push_back(_tmpOffset + _offset);
    }

    return true;
};
bool emotefile::parseResource(int32_t& id, uint32_t _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::ResourceN1:
        case PSB::PSBObjType::ResourceN2:
        case PSB::PSBObjType::ResourceN3:
        case PSB::PSBObjType::ResourceN4:
        {
            id = 0;
            filePtr->ReadBuffer(&id,
                                typeByte - static_cast<int8_t>(PSB::PSBObjType::ResourceN1) + 1);
            return true;
        }
        default:
            TVPConsoleLog("Invalidate Resource");
            return false;
    }
}
uint64_t emotefile::GetSize()
{
    return filePtr->GetSize();
}
void emotefile::ReadAllData(uint8_t* output, uint32_t outputlen)
{
    filePtr->SetPosition(0);
    filePtr->ReadBuffer(output, outputlen);
}
bool emotefile::GenerateAniTree()
{
    // baseObj
    std::map<std::string, uint32_t> _rootData;
    parseObject(_rootData, _header.offsetEntries);
    // isemote
    std::string typeName;
    parseString(typeName, _rootData["spec"]);
    if (strcmp(typeName.c_str(), "krkr") == 0) // emote
    {
        isKrkr = true;
    }
    else if (strcmp(typeName.c_str(), "win") == 0) // motion
    {
        isKrkr = false;
    }
    else if (strcmp(typeName.c_str(), "common") == 0)
    {
        isKrkr = false;
        colorType = 1;
    }
    else
    {
        TVPConsoleLog("unknown emoteType!!!");
    }
    // metadata
    _metadata = new emotemetadata(this, _rootData["metadata"]);
    // objs
    std::map<std::string, uint32_t> _tmpMap;
    parseObject(_tmpMap, _rootData["object"]);
    for (auto _obj : _tmpMap)
    {
        emoteobject* tmp = new emoteobject(this, _obj.second);
        tmp->name = _obj.first;
        _objects.insert(std::pair<std::string, emoteobject*>(_obj.first, tmp));
    }
    // screen
    parseObject(_tmpMap, _rootData["screenSize"]);
    int64_t tmp;
    parseNumber(tmp, _tmpMap["originX"]);
    _screenSize.originX = static_cast<int32_t>(tmp);
    parseNumber(tmp, _tmpMap["originY"]);
    _screenSize.originY = static_cast<int32_t>(tmp);
    parseNumber(tmp, _tmpMap["width"]);
    _screenSize.width = static_cast<int32_t>(tmp);
    parseNumber(tmp, _tmpMap["height"]);
    _screenSize.height = static_cast<int32_t>(tmp);
    // source
    parseObject(_tmpMap, _rootData["source"]);
    for (auto _obj : _tmpMap)
    {
        emotesource* tmp = new emotesource(this, _obj.second);
        _source.insert(std::pair<std::string, emotesource*>(_obj.first, tmp));
    }
    // stereovisionProfile
    parseObject(_tmpMap, _rootData["stereovisionProfile"]);
    parseReal(_stereovisionProfile.dist_e2d, _tmpMap["dist_e2d"]);
    parseReal(_stereovisionProfile.dist_eye, _tmpMap["dist_eye"]);
    parseReal(_stereovisionProfile.eye_angle_ltd, _tmpMap["eye_angle_ltd"]);
    parseReal(_stereovisionProfile.f_level, _tmpMap["f_level"]);
    parseReal(_stereovisionProfile.fov, _tmpMap["fov"]);
    parseReal(_stereovisionProfile.len_disp, _tmpMap["len_disp"]);

    // getLastSyncTime
    for (auto _objTmp : _objects)
    {
        for (auto _tmpMtn : _objTmp.second->motion)
        {
            if (_tmpMtn.second->isParameterize) continue;
            std::vector<emotenode*> _stack(_tmpMtn.second->nodeList.begin(),
                                           _tmpMtn.second->nodeList.end());
            while (!_stack.empty())
            {
                emotenode* _current = _stack.back();
                _stack.pop_back();

                for (auto frameTmp : _current->frameList)
                {
                    if (frameTmp != nullptr && frameTmp->hasContent)
                    {
                        if (!_current->isParameterize && frameTmp->time > _tmpMtn.second->syncTime)
                            _tmpMtn.second->syncTime = frameTmp->time;

                        emotemotion* _emot = findmotionByName(frameTmp->src);
                        if (_emot != nullptr)
                        {
                            for (auto it = _emot->nodeList.begin(); it != _emot->nodeList.end();
                                 ++it)
                            {
                                _stack.push_back(*it);
                            }
                        }
                    }
                }
            }
        }
    }
    // mark attrcomp
    for (auto attrItm : _metadata->_attrcomp)
    {
        if (attrItm == nullptr)
            continue;
        for (auto removeDataItm : attrItm->data_remove)
        {
            if (removeDataItm.value > 0)
                continue;
            auto cha = _objects.find(removeDataItm.chara.c_str());
            if (cha != _objects.end())
            {
                auto mtn = cha->second->motion.find(removeDataItm.motion.c_str());
                if (mtn != cha->second->motion.end())
                {
                    auto lay = mtn->second->getNodeByName(removeDataItm.layer);
                    if (lay != nullptr)
                    {
                        lay->removed = true;
                    }
                }
            }
        }
    }
    // selectValueSet
    for (auto selcItm : _metadata->_selectorControl)
    {
        selcItm->selectValue(0);
    }

    return true;
}
bool emotefile::ClearAniTree()
{
    if (_metadata != nullptr)
        delete _metadata;
    for (auto obj : _objects)
    {
        if (obj.second != nullptr)
            delete obj.second;
    }
    for (auto src : _source)
    {
        if (src.second != nullptr)
            delete src.second;
    }
    return true;
}
void emotefile::updateZMax(float zMax)
{
    if (abs(zMax) > _zMax)
    {
        _zMax = abs(zMax);
    }
}
float emotefile::getZMax()
{
    return _zMax;
}
void emotefile::updateSyncTime(float _syn)
{
    if (_syn > _syncTime)
    {
        _syncTime = _syn;
    }
}
float emotefile::getSyncTime()
{
    return _syncTime;
}
emoteicon* emotefile::findsourceByName(const std::string& name)
{
    std::istringstream iss(name);
    std::string token;

    // 源头
    std::getline(iss, token, '/');
    if (strcmp(token.c_str(), "src") == 0)
    {
        // 一级路径
        std::getline(iss, token, '/');
        auto tmp = _source.find(token.c_str());
        if (tmp != _source.end())
        {
            emotesource* src = tmp->second;
            // 二级路径
            std::getline(iss, token, '/');
            auto tmp1 = src->icon.find(token.c_str());
            if (tmp1 != src->icon.end())
            {
                return tmp1->second;
            }
        }
        TVPConsoleLog("src find failed!!!");
    }
    else
    {
        return nullptr;
    }
    return nullptr;
}
emotemotion* emotefile::findmotionByName(const std::string& name)
{
    std::istringstream iss(name);
    std::string token, token1, token2;

    // 源头
    std::getline(iss, token, '/');
    if (strcmp(token.c_str(), "motion") == 0)
    {
        std::getline(iss, token1, '/');
        std::getline(iss, token2, '/');
        // 一级路径
        auto tmp = _objects.find(token1.c_str());
        if (tmp != _objects.end())
        {
            emoteobject* src = tmp->second;
            // 二级路径
            auto tmp1 = src->motion.find(token2.c_str());
            if (tmp1 != src->motion.end())
            {
                return tmp1->second;
            }
        }
        TVPConsoleLog("motion find failed!!!");
    }
    else
    {
        return nullptr;
    }
    return nullptr;
}
bool emotefile::readIconTobuffer(uint8_t* buff, uint32_t buffSize, uint32_t pitch, emoteicon* ic)
{
    if (ic->pixel < 0 || ic->pixel >= chunkLengths.size())
        return false;
    // 生数据读取
    uint8_t* srcbuff = new uint8_t[chunkLengths.at(ic->pixel)];
    filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(ic->pixel));
    filePtr->ReadBuffer(srcbuff, chunkLengths.at(ic->pixel));
    // 解码方案
    if (strcmp(ic->compress.c_str(), "RL") == 0)
    {
        if (ic->pal > -1)
        {
            const uint32_t palSize = chunkLengths.at(ic->pal);
            uint8_t* palbuff = new uint8_t[palSize];
            filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(ic->pal));
            filePtr->ReadBuffer(palbuff, chunkLengths.at(ic->pal));

            auto copyPaletteColor = [&](uint8_t* dst, uint8_t colorIndex) {
                const uint32_t paletteOffset = (uint32_t)colorIndex * 4;
                if (paletteOffset + 4 > palSize)
                {
                    memset(dst, 0, 4);
                    return;
                }
                memcpy(dst, palbuff + paletteOffset, 4);
                };

            uint32_t currSize = 0;
            uint8_t* currPtr = srcbuff;
            uint8_t* endPtr = currPtr + chunkLengths.at(ic->pixel);
            uint8_t* currDst = buff;
            while (currPtr < endPtr && currSize < buffSize)
            {
                int current = *currPtr;
                currPtr++;
                int count;
                if ((current & 0x80) != 0) // Redundant
                {
                    count = (current ^ 0x80) + 3;
                    uint8_t coloridx = *currPtr;
                    currPtr += 1;
                    for (int i = 0; i < count; i++)
                    {
                        if (currSize + 4 > buffSize)
                            break;
                        copyPaletteColor(currDst, coloridx);
                        currDst += 4;
                        currSize += 4;
                    }
                }
                else // not redundant
                {
                    count = (current + 1);
                    if (currSize + count * 4 > buffSize)
                        count = (buffSize - currSize) / 4;
                    for (int i = 0; i < count; i++)
                    {
                        copyPaletteColor(currDst + i * 4, *(currPtr + i));
                    }
                    currPtr += count;
                    currDst += count * 4;
                    currSize += count * 4;
                }
            }
        }
        else
        {
            uint32_t currSize = 0;
            uint8_t* currPtr = srcbuff;
            uint8_t* endPtr = currPtr + chunkLengths.at(ic->pixel);
            uint8_t* currDst = buff;
            while (currPtr < endPtr && currSize < buffSize)
            {
                int current = *currPtr;
                currPtr++;
                int count;
                if ((current & 0x80) != 0) // Redundant
                {
                    count = (current ^ 0x80) + 3;
                    uint32_t color = *((uint32_t*)currPtr);
                    currPtr += 4;
                    for (int i = 0; i < count; i++)
                    {
                        if (currSize + 4 > buffSize)
                            break;
                        memcpy(currDst, &color, 4);
                        currDst += 4;
                        currSize += 4;
                    }
                }
                else // not redundant
                {
                    count = (current + 1) * 4;
                    if (currSize + count > buffSize)
                        count = buffSize - currSize;
                    memcpy(currDst, currPtr, count);
                    currPtr += count;
                    currDst += count;
                    currSize += count;
                }
            }
        }
    }
    else if (strcmp(ic->compress.c_str(), "none") == 0)
    {
        if (ic->pal >= 0)
        {
            // Uncompressed indexed images still require palette expansion.
            // Copying the index bytes as RGBA collapses M2 hair/stroke to a few faint rows.
            if (ic->pal >= static_cast<int32_t>(chunkLengths.size()) || ic->width <= 0 ||
                ic->height <= 0 || pitch == 0)
            {
                delete[] srcbuff;
                return false;
            }
            const uint32_t paletteSize = chunkLengths.at(static_cast<size_t>(ic->pal));
            std::vector<uint8_t> palette(paletteSize);
            filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(static_cast<size_t>(ic->pal)));
            filePtr->ReadBuffer(palette.data(), palette.size());

            const uint32_t width = static_cast<uint32_t>(ic->width);
            const uint32_t pixelCount = std::min(
                chunkLengths.at(static_cast<size_t>(ic->pixel)),
                static_cast<uint32_t>(ic->width * ic->height));
            for (uint32_t i = 0; i < pixelCount; ++i)
            {
                const size_t destination = static_cast<size_t>(i / width) * pitch +
                                           static_cast<size_t>(i % width) * 4;
                if (destination + 4 > buffSize)
                    break;
                const size_t paletteOffset = static_cast<size_t>(srcbuff[i]) * 4;
                if (paletteOffset + 4 <= palette.size())
                    memcpy(buff + destination, palette.data() + paletteOffset, 4);
                else
                    memset(buff + destination, 0, 4);
            }
        }
        else
        {
            uint32_t cpySize = std::min(chunkLengths.at(ic->pixel), buffSize);
            memcpy(buff, srcbuff, cpySize);
        }
    }
    else
    {
        if (!isKrkr) // 采用裁切贴图
        {
            if (strcmp(ic->type.c_str(), "RGBA8") == 0)
            {
                uint8_t* start = srcbuff + (int)(ic->top * ic->texWidth + ic->left) * 4;
                uint32_t cppitch = std::min(
                    (uint32_t)(std::min(ic->left + ic->width, ic->texWidth) - ic->left) * 4, pitch);
                uint32_t height =
                    std::min((uint32_t)(std::min(ic->top + ic->height, ic->texHeight) - ic->top),
                             buffSize / pitch);
                for (uint32_t i = 0; i < height; i++)
                {
                    memcpy(buff + i * pitch, start + (int)(ic->texWidth * 4 * i), cppitch);
                }
            }
            else
            {
                TVPConsoleLog("unknown icon format");
                delete[] srcbuff;
                return false;
            }
        }
        else
        {
            TVPConsoleLog("unknown icon format");
            delete[] srcbuff;
            return false;
        }
    }
    delete[] srcbuff;
    return true;
}
bool emotefile::getTickByName(const std::string& name, float& ret)
{
    if (_metadata == nullptr)
        return false;
    auto it = _metadata->_varList.find(name);
    if (it == _metadata->_varList.end())
        return false;
    ret = it->second;
    return true;
}
void emotefile::setVariable(const std::string& name, tjs_real value)
{
    // _selectorControl
    if (!isMotion)
    {
        for (auto _selItm : _metadata->_selectorControl)
        {
            if (strcmp(_selItm->lable.c_str(), name.c_str()) == 0)
            {
                _selItm->selectValue(value);
                return;
            }
        }

        // _varList
        auto varPos = _metadata->_varList.find(name);
        if (varPos != _metadata->_varList.end())
        {
            varPos->second = value;
        }
    }
}
} // namespace emoteplayer
