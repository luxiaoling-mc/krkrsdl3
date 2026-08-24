#pragma once

#include "ComplexRect.h"
#include <unordered_map>
#include <stdint.h>
#include <string>

// 由于gpu兼容软渲染，但软渲染不兼容gpu
// 所以对于内核我们按照krkr2/krkrz原版思路，保持完全软渲染
// gpu加速则放入插件中，使用特殊的Layer和DrawDevice来实现
struct TVPTextureFormat
{
    enum e
    {
        None = 0,
        Gray = 1,
        RGB = 3,
        RGBA = 4,
        // for opengl compressed texture, the argument pitch = block_width | (block_height << 16) or
        // block_size
        Compressed = 0x10000,
        CompressedEnd = 0x20000,
    };
};

class iTVPTexture2D
{
protected:
    int RefCount;
    tjs_int Width;  // actual width
    tjs_int Height; // actual height
    // int Flags, TexWidth, TexHeight, ActualWidth, ActualHeight;
    iTVPTexture2D(tjs_int w, tjs_int h) : Width(w), Height(h), RefCount(1) {}

public:
    virtual ~iTVPTexture2D(){};
    void AddRef() { ++RefCount; }
    virtual void Release();
    tjs_uint GetWidth() const { return Width; }
    tjs_uint GetHeight() const { return Height; }
    virtual tjs_uint GetInternalWidth() const { return Width; }
    virtual tjs_uint GetInternalHeight() const { return Height; }
    virtual void SetSize(unsigned int w, unsigned int h)
    { // may lost image content
        Width = w;
        Height = h;
    }

    virtual TVPTextureFormat::e GetFormat() const = 0;
    virtual const void* GetScanLineForRead(tjs_uint l) { return nullptr; }
    virtual const void* GetPixelData() { return GetScanLineForRead(0); }
    virtual void* GetScanLineForWrite(tjs_uint l) { return (void*)GetScanLineForRead(l); }
    virtual tjs_int GetPitch() const { return 0x100000; }
    bool IsIndependent() const { return RefCount == 1; }

    // virtual tGLTexture* GetTexture() = 0;
    virtual void Update(const void* pixel,
                        TVPTextureFormat::e format,
                        int pitch,
                        const tTVPRect& rc) = 0;
    virtual uint32_t GetPoint(int x, int y) = 0;
    virtual void SetPoint(int x, int y, uint32_t clr) = 0;
    virtual bool IsStatic() = 0; // aka. is readonly
    virtual bool IsOpaque() = 0;
    virtual bool GetTextureData(void* picData, tjs_int& pic_pitch) = 0;
    virtual bool GetScale(float& x, float& y)
    {
        x = 1.f;
        y = 1.f;
        return true;
    }

    // ---- 纹理驻留与显式同步（防 GPU→CPU 隐式回读，见 docs/gpu-readback-design.md）----
    // 默认实现为 CPU 驻留（软件纹理，零拷贝）；GPU 纹理实现需 override。
    // 原则：读不切换驻留（只从缓存读）、写才标记脏、单帧同一纹理至多一次回读+一次上传。
    virtual bool IsCPUResident() const { return true; }
    bool IsGPUResident() const { return !IsCPUResident(); }
    // GPU 驻留时的后端纹理句柄（供上屏 sprite 别名，零拷贝）；CPU 驻留返回 nullptr
    virtual void* GetTextureHandle() { return nullptr; }
    // 显式回读（带缓存）：返回 CPU 像素；软件实现零拷贝返回真实缓冲
    virtual void* LockCPURead() { return const_cast<void*>(GetPixelData()); }
    virtual void UnlockCPU() {}
    // CPU 数据已修改，下次 GPU 使用前需上传
    virtual void MarkCPUModified() {}
    // 纹理已驻留 GPU，CPU 缓存失效
    virtual void InvalidateCPUCache() {}

    static void RecycleProcess();
};

class iTVPRenderMethod
{
protected:
    virtual ~iTVPRenderMethod() {} // undeletable
    std::string Name;

public:
    // the parameter id should not change in whole lifecycle, valid id >= 0
    virtual int EnumParameterID(const char* name) { return -1; };
    virtual void SetParameterUInt(int id, unsigned int Value){};
    virtual void SetParameterInt(int id, int Value){};
    virtual void SetParameterPtr(int id, const void* Value){};
    virtual void SetParameterFloat(int id, float Value){};
    virtual void SetParameterColor4B(int id, unsigned int clr){};
    virtual void SetParameterOpa(int id, int Value){};
    virtual void SetParameterFloatArray(int id, float* Value, int nElem){};
    virtual iTVPRenderMethod* SetBlendFuncSeparate(
        int func, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha)
    {
        return this;
    }
    virtual bool IsBlendTarget() { return true; }
    void SetName(const std::string& name) { Name = name; }
    const std::string& GetName() { return Name; }
};

template<typename TElem>
class tRenderTextureArray
{
    const std::pair<iTVPTexture2D*, TElem>* pElem;
    size_t nCount;

public:
    typedef std::pair<iTVPTexture2D*, TElem> Element;
    tRenderTextureArray() : pElem(nullptr), nCount(0) {}
    tRenderTextureArray(std::pair<iTVPTexture2D*, TElem>* p, size_t n) : pElem(p), nCount(n) {}

    template<typename T>
    tRenderTextureArray(const T& arr)
    {
        pElem = arr;
        nCount = sizeof(arr) / sizeof(arr[0]);
    }

    const std::pair<iTVPTexture2D*, TElem>& operator[](size_t i) const { return pElem[i]; }

    size_t size() const { return nCount; }
};

typedef tRenderTextureArray<tTVPRect> tRenderTexRectArray;
typedef tRenderTextureArray<const tTVPPointD*> tRenderTexQuadArray;

class tTVPBitmap;
namespace TJS
{
class tTJSBinaryStream;
}
class iTVPRenderManager
{
protected:
    virtual ~iTVPRenderManager() {} // undeletable
    void RegisterRenderMethod(const char* name, iTVPRenderMethod* method);
    virtual iTVPRenderMethod* GetRenderMethodFromScript(const char* script,
                                                        int nTex,
                                                        unsigned int flags)
    {
        return nullptr;
    }
    std::unordered_map<uint32_t, iTVPRenderMethod*> AllMethods;

public:
    void Initialize();

public:
#define RENDER_CREATE_TEXTURE_FLAG_ANY 0
#define RENDER_CREATE_TEXTURE_FLAG_STATIC 1
#define RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS 2
    virtual iTVPTexture2D* CreateTexture2D(const void* pixel,
                                           int pitch,
                                           unsigned int w,
                                           unsigned int h,
                                           TVPTextureFormat::e format,
                                           int flags = RENDER_CREATE_TEXTURE_FLAG_ANY) = 0;
    virtual iTVPTexture2D* CreateTexture2D(tTVPBitmap* bmp) = 0; // for province image
    virtual iTVPTexture2D* CreateTexture2D(
        TJS::tTJSBinaryStream* s) = 0;      // for compressed or special image format
    virtual iTVPTexture2D* CreateTexture2D( // create and copy content from exist texture
        unsigned int neww,
        unsigned int newh,
        iTVPTexture2D* tex) = 0;

    // each method is singleton in whole lifecycle
    virtual iTVPRenderMethod* GetRenderMethod(const char* name, uint32_t* hint = nullptr);
#define RENDER_METHOD_FLAG_NONE 0
#define RENDER_METHOD_FLAG_TARGET_AS_INPUT 1
    iTVPRenderMethod* CompileRenderMethod(const char* name,
                                          const char* glsl_script,
                                          int nTex,
                                          unsigned int flags = 0);
    iTVPRenderMethod* GetOrCompileRenderMethod(const char* name,
                                               uint32_t* hint,
                                               const char* glsl_script,
                                               int nTex,
                                               unsigned int flags = 0);

    virtual bool IsSoftware() { return false; }
    virtual const char* GetName() = 0;

    virtual bool GetRenderStat(unsigned int& drawCount, uint64_t& vmemsize) = 0;
    virtual bool GetTextureStat(iTVPTexture2D* texture, uint64_t& vmemsize) { return false; }

    virtual void BeginStencil(iTVPTexture2D* reftex) {}
    virtual void EndStencil() {}

    virtual void SetRenderTarget(iTVPTexture2D* target) {} // for manual rendering

    // interface to access custom parameter
    virtual int EnumParameterID(const char* name) { return -1; }
    virtual void SetParameterUInt(int id, unsigned int Value){};
    virtual void SetParameterInt(int id, int Value){};
    virtual void SetParameterPtr(int id, const void* Value){};
    virtual void SetParameterFloat(int id, float Value){};

    // -------------- operations ----------------
    // dst x Tex1 x ... x TexN -> dst
    // referenced target texture would be used if target texture is required as source
    virtual void OperateRect(iTVPRenderMethod* method,
                             iTVPTexture2D* tar,
                             iTVPTexture2D* reftar,
                             const tTVPRect& rctar,
                             const tRenderTexRectArray& textures) = 0;

    // src x dst -> tar
    virtual void OperateTriangles(iTVPRenderMethod* method,
                                  int nTriangles,
                                  iTVPTexture2D* target,
                                  iTVPTexture2D* reftar,
                                  const tTVPRect& rcclip,
                                  const tTVPPointD* pttar,
                                  const tRenderTexQuadArray& textures) = 0;

    // src -> tar
    virtual void OperatePerspective(iTVPRenderMethod* method,
                                    int nQuads,
                                    iTVPTexture2D* target,
                                    iTVPTexture2D* reftar,
                                    const tTVPRect& rcclip,
                                    const tTVPPointD* pttar /*quad{lt,rt,lb,rb}*/,
                                    const tRenderTexQuadArray& textures) = 0;

public:
    // utility function
    iTVPRenderMethod* GetRenderMethod(tjs_int opa, bool hda, int /*tTVPBBBltMethod*/ method);
    struct tRenderMethodCache* RenderMethodCache = nullptr;
};

void TVPRegisterRenderManager(const char* name, iTVPRenderManager* (*func)());

#define REGISTER_RENDERMANAGER(MGR, NAME) \
    static iTVPRenderManager* __##MGR##Factory() \
    { \
        return new MGR(); \
    } \
    static class __##MGR##AutoReigster \
    { \
    public: \
        __##MGR##AutoReigster() \
        { \
            TVPRegisterRenderManager(#NAME, __##MGR##Factory); \
        } \
    } __##MGR##AutoReigster_instance;

iTVPRenderManager* TVPGetRenderManager();
iTVPRenderManager* TVPGetRenderManager(const ttstr& name);
bool TVPIsSoftwareRenderManager();
// 切换当前渲染管理器（供插件如 DrawDeviceD3D 注入 GPU 渲染管理器；
// 传 nullptr 恢复默认软件渲染）
void TVPSetRenderManager(iTVPRenderManager* mgr);
// 软件渲染管理器（province image 等固定使用；也供 GPU 管理器做软件回退）
iTVPRenderManager* TVPGetSoftwareRenderManager();
