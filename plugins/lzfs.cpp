#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "TVPStorage.h"
#include "Platform.h"

#define NCB_MODULE_NAME TJS_N("lzfs.dll")

class LZFSMedia : public iTVPStorageMedia
{
public:
    LZFSMedia() : refCount(1) {}

    virtual void AddRef() override { refCount++; };

    virtual void Release() override
    {
        if (refCount == 1)
        {
            delete this;
        }
        else
        {
            refCount--;
        }
    };

    // returns media name like "file", "http" etc.
    virtual void GetName(ttstr& name) override { name = TJS_N("lzfs"); }

    //	virtual ttstr IsCaseSensitive() = 0;
    // returns whether this media is case sensitive or not

    // normalize domain name according with the media's rule
    virtual void NormalizeDomainName(ttstr& name) override
    {
        // nothing to do
    }

    // normalize path name according with the media's rule
    virtual void NormalizePathName(ttstr& name) override
    {
        // nothing to do
    }

    // check file existence
    virtual bool CheckExistentStorage(const ttstr& name) override
    {
        return false;
    }

    // open a storage and return a tTJSBinaryStream instance.
    // name does not contain in-archive storage name but
    // is normalized.
    virtual tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags) override
    {
        return nullptr;
    }

    // list files at given place
    virtual void GetListAt(const ttstr& name, iTVPStorageLister* lister) override
    {
        // nothing to do
    }

    // basically the same as above,
    // check wether given name is easily accessible from local OS filesystem.
    // if true, returns local OS native name. otherwise returns an empty string.
    virtual void GetLocallyAccessibleName(ttstr& name) override
    {
        // nothing to do
    }

protected:
    /**
     * デストラクタ
     */
    virtual ~LZFSMedia() {}

private:
    tjs_uint refCount; //< リファレンスカウント
};
static LZFSMedia* _lzfsMedia = nullptr;

void lzfs_init()
{
    // 注册media
    if (_lzfsMedia == nullptr)
    {
        _lzfsMedia = new LZFSMedia();
        TVPRegisterStorageMedia(_lzfsMedia);
    }
}

void lzfs_done()
{
    // 解注media
    if (_lzfsMedia != nullptr)
    {
        TVPUnregisterStorageMedia(_lzfsMedia);
        _lzfsMedia->Release();
        _lzfsMedia = nullptr;
    }
}

NCB_PRE_REGIST_CALLBACK(lzfs_init);
NCB_POST_UNREGIST_CALLBACK(lzfs_done);