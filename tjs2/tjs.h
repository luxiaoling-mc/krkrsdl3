//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

     See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "tTJS" script language API class implementation
//---------------------------------------------------------------------------

#pragma once

#include "tjsVariant.h"

#include <atomic>
#include <vector>
#include <deque>

namespace TJS
{
//---------------------------------------------------------------------------
// TJS version
//---------------------------------------------------------------------------
extern const tjs_int TJSVersionMajor;
extern const tjs_int TJSVersionMinor;
extern const tjs_int TJSVersionRelease;
extern const tjs_int TJSVersionHex;

extern tjs_char TJSCompiledDate[];
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Console output callback interface
//---------------------------------------------------------------------------
class iTJSConsoleOutput
{
public:
    virtual void ExceptionPrint(const tjs_char* msg) = 0;
    virtual void Print(const tjs_char* msg) = 0;
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Object Hash Size Limit ( must be larger than or equal to 0 )
//---------------------------------------------------------------------------
extern tjs_int TJSObjectHashBitsLimit;

//---------------------------------------------------------------------------
// global options
//---------------------------------------------------------------------------
extern bool TJSEvalOperatorIsOnGlobal;
// Post-! operator (evaluate expression) is to be executed on "this" context
// since TJS2 2.4.1.
// Turn this switch true makes post-! operator running on global context,
// like TJS2 before 2.4.1.
extern bool TJSWarnOnNonGlobalEvalOperator;
// Output warning against non-local post-! operator.
// (For checking where the post-! operators are used)
extern bool TJSEnableDebugMode;
// Enable TJS2 Debugging support. Enabling this may make the
// program somewhat slower and using more memory.
// Do not use this mode unless you want to debug the program.
extern bool TJSWarnOnExecutionOnDeletingObject;
// Output warning against running code on context of
// deleting-in-progress object. This is available only the Debug mode is
// enabled.
extern bool TJSUnaryAsteriskIgnoresPropAccess;
// Unary '*' operator means accessing property object directly without
// normal property access, if this options is set true.
// This is replaced with '&' operator since TJS2 2.4.15. Turn true for
// gaining old compatibility.

//---------------------------------------------------------------------------
// tTJS class - "tTJS" TJS API Class
//---------------------------------------------------------------------------
class tTJSScriptBlock;
class tTJSPPMap;
class tTJSCustomObject;
class tTJSScriptCache;
class tTJS
{
    friend class tTJSScriptBlock;
public:
    tTJS();
    ~tTJS();

private:
    tTJSPPMap* PPValues;

    std::vector<tTJSScriptBlock*> ScriptBlocks;

    iTJSConsoleOutput* ConsoleOutput;

    tTJSCustomObject* Global;

    tTJSScriptCache* Cache;
    class tTJSVariantArrayStack* VariantArrayStack = nullptr;

public:
    iTJSDispatch2* GetGlobal();
    iTJSDispatch2* GetGlobalNoAddRef() const;

    tTJSVariantArrayStack* GetVariantArrayStack() { return VariantArrayStack; }

private:
    void AddScriptBlock(tTJSScriptBlock* block);
    void RemoveScriptBlock(tTJSScriptBlock* block);

public:
    void SetConsoleOutput(iTJSConsoleOutput* console);
    iTJSConsoleOutput* GetConsoleOutput() const { return ConsoleOutput; };
    void OutputToConsole(const tjs_char* msg) const;
    void OutputExceptionToConsole(const tjs_char* msg) const;
    void OutputToConsoleWithCentering(const tjs_char* msg, tjs_uint width) const;
    void OutputToConsoleSeparator(const tjs_char* text, tjs_uint count) const;

    void Dump(tjs_uint width = 80) const; // dumps all existing script block

    void ExecScript(const tjs_char* script,
                    tTJSVariant* result = NULL,
                    iTJSDispatch2* context = NULL,
                    const tjs_char* name = NULL,
                    tjs_int lineofs = 0);

    void ExecScript(const ttstr& script,
                    tTJSVariant* result = NULL,
                    iTJSDispatch2* context = NULL,
                    const ttstr* name = NULL,
                    tjs_int lineofs = 0);

    void EvalExpression(const tjs_char* expression,
                        tTJSVariant* result,
                        iTJSDispatch2* context = NULL,
                        const tjs_char* name = NULL,
                        tjs_int lineofs = 0);

    void EvalExpression(const ttstr& expression,
                        tTJSVariant* result,
                        iTJSDispatch2* context = NULL,
                        const ttstr* name = NULL,
                        tjs_int lineofs = 0);

    void SetPPValue(const tjs_char* name, const tjs_int32 value);
    tjs_int32 GetPPValue(const tjs_char* name);

    void DoGarbageCollection();

    // for Bytecode
    void LoadByteCode(const tjs_uint8* buff,
                      size_t len,
                      tTJSVariant* result = NULL,
                      iTJSDispatch2* context = NULL,
                      const tjs_char* name = NULL);

    bool LoadByteCode(class tTJSBinaryStream* stream,
                      tTJSVariant* result = NULL,
                      iTJSDispatch2* context = NULL,
                      const tjs_char* name = NULL);

    // for Binary Dictionay Array
    static bool LoadBinaryDictionayArray(class tTJSBinaryStream* stream, tTJSVariant* result);

    void CompileScript(const tjs_char* script,
                       class tTJSBinaryStream* output,
                       bool isresultneeded = false,
                       bool outputdebug = false,
                       bool isexpression = false,
                       const tjs_char* name = NULL,
                       tjs_int lineofs = 0);
};
//---------------------------------------------------------------------------

/*[*/
//---------------------------------------------------------------------------
// iTJSTextStream - used by Array.save/load Dictionaty.save/load
//---------------------------------------------------------------------------
class tTJSString;
class iTJSTextReadStream
{
public:
    virtual tjs_uint Read(tTJSString& targ, tjs_uint size) = 0;
    virtual void Destruct() = 0; // must delete itself
    virtual ~iTJSTextReadStream() {}
};
//---------------------------------------------------------------------------
class iTJSTextWriteStream
{
public:
    virtual void Write(const tTJSString& targ) = 0;
    virtual void Destruct() = 0; // must delete itself
    virtual ~iTJSTextWriteStream() {}
};
//---------------------------------------------------------------------------
extern iTJSTextReadStream* (*TJSCreateTextStreamForRead)(const tTJSString& name,
                                                         const tTJSString& modestr);
extern iTJSTextWriteStream* (*TJSCreateTextStreamForWrite)(const tTJSString& name,
                                                           const tTJSString& modestr);
extern class tTJSBinaryStream* (*TJSCreateBinaryStreamForRead)(const tTJSString& name,
                                                               const tTJSString& modestr);
extern class tTJSBinaryStream* (*TJSCreateBinaryStreamForWrite)(const tTJSString& name,
                                                                const tTJSString& modestr);
//---------------------------------------------------------------------------

/*]*/
/*[*/
//---------------------------------------------------------------------------
// tTJSBinaryStream constants
//---------------------------------------------------------------------------
#define TJS_BS_READ 0
#define TJS_BS_WRITE 1
#define TJS_BS_APPEND 2
#define TJS_BS_UPDATE 3

#define TJS_BS_DELETE_ON_CLOSE 0x10

#define TJS_BS_ACCESS_MASK 0x0f
#define TJS_BS_OPTION_MASK 0xf0

#define TJS_BS_SEEK_SET 0
#define TJS_BS_SEEK_CUR 1
#define TJS_BS_SEEK_END 2
//---------------------------------------------------------------------------

/*]*/

//---------------------------------------------------------------------------
// tTJSBinaryStream base stream class
//---------------------------------------------------------------------------
class tTJSBinaryStream
{
private:
public:
    //-- must implement
    virtual tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) = 0;
    /* if error, position is not changed */

    //-- optionally to implement
    virtual tjs_uint Read(void* buffer, tjs_uint read_size) = 0;
    /* returns actually read size */

    virtual tjs_uint Write(const void* buffer, tjs_uint write_size) = 0;
    /* returns actually written size */

    virtual bool Flush() = 0;
    /* returns flush status */

    virtual void SetEndOfStorage();
    // the default behavior is raising a exception
    /* if error, raises exception */

    //-- should re-implement for higher performance
    virtual tjs_uint64 GetSize() = 0;

    virtual ~tTJSBinaryStream() { ; }

    tjs_uint64 GetPosition();

    void SetPosition(tjs_uint64 pos);

    void ReadBuffer(void* buffer, tjs_uint read_size);
    void WriteBuffer(const void* buffer, tjs_uint write_size);

    tjs_uint64 ReadI64LE(); // reads little-endian integers
    tjs_uint32 ReadI32LE();
    tjs_uint16 ReadI16LE();
    tjs_uint8 ReadI8LE();
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSObjectPool
//---------------------------------------------------------------------------
template<typename T>
class tTJSObjectPool
{
    struct Slot
    {
        std::atomic<T*> obj{nullptr};
        std::atomic<uint32_t> gen{0};
    };

    // 使用 deque 避免元素移动
    std::deque<Slot> slots;
    std::atomic<size_t> size{0};
    std::atomic<size_t> head{0};

    // 空闲栈
    std::vector<size_t> freeStack;
    std::atomic<size_t> freeStackSize{0};

public:
    tTJSObjectPool(size_t initialCapacity = 1024)
    {
        slots.resize(initialCapacity);
        freeStack.resize(initialCapacity);
        for (size_t i = 0; i < initialCapacity; ++i)
        {
            freeStack[i] = i;
        }
        head = initialCapacity;
        freeStackSize = initialCapacity;
    }

    size_t registerObject(T* obj)
    {
        if (!obj)
            return (size_t)-1;

        size_t idx = popFreeSlot();
        if (idx == (size_t)-1)
        {
            // 扩展池子
            idx = expand();
            // 如果扩展后仍然失败，返回错误
            if (idx == (size_t)-1)
                return (size_t)-1;
        }

        slots[idx].obj.store(obj, std::memory_order_release);
        slots[idx].gen.fetch_add(1, std::memory_order_acq_rel);
        return idx;
    }

    void unregisterObject(size_t idx)
    {
        if (idx >= slots.size())
            return;
        slots[idx].obj.store(nullptr, std::memory_order_release);
        pushFreeSlot(idx);
    }

    void clear()
    {
        // 重置所有槽位
        for (auto& slot : slots)
        {
            slot.obj.store(nullptr, std::memory_order_relaxed);
            slot.gen.store(0, std::memory_order_relaxed);
        }

        // 重置空闲栈
        size_t stackSize = freeStackSize.load(std::memory_order_acquire);
        for (size_t i = 0; i < stackSize; ++i)
        {
            freeStack[i] = i;
        }
        head.store(stackSize, std::memory_order_release);
        size.store(0, std::memory_order_release);
    }

    template<typename Func>
    void forEach(Func func)
    {
        size_t currentSize = size.load(std::memory_order_acquire);
        for (size_t i = 0; i < currentSize; ++i)
        {
            T* obj = slots[i].obj.load(std::memory_order_acquire);
            if (obj)
            {
                func(obj);
            }
        }
    }

private:
    size_t popFreeSlot()
    {
        size_t oldHead = head.load(std::memory_order_acquire);
        while (true)
        {
            if (oldHead == 0)
                return (size_t)-1;
            size_t newHead = oldHead - 1;
            size_t idx = freeStack[newHead];
            if (head.compare_exchange_weak(oldHead, newHead, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
            {
                return idx;
            }
        }
    }

    void pushFreeSlot(size_t idx)
    {
        size_t oldHead = head.load(std::memory_order_acquire);
        while (true)
        {
            size_t newHead = oldHead + 1;
            size_t stackSize = freeStackSize.load(std::memory_order_acquire);
            if (newHead >= stackSize)
            {
                growFreeStack();
                // 重新加载 head，因为 growFreeStack 可能改变了它
                oldHead = head.load(std::memory_order_acquire);
                continue;
            }
            freeStack[oldHead] = idx;
            if (head.compare_exchange_weak(oldHead, newHead, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
            {
                return;
            }
        }
    }

    size_t expand()
    {
        size_t oldSize = size.load(std::memory_order_acquire);
        size_t newSize = oldSize * 2 + 1024;

        // 先尝试压入新槽位
        for (size_t i = oldSize; i < newSize; ++i)
        {
            // 直接压入，不通过 pushFreeSlot（避免递归）
            size_t oldHead = head.load(std::memory_order_acquire);
            while (true)
            {
                size_t newHead = oldHead + 1;
                size_t stackSize = freeStackSize.load(std::memory_order_acquire);
                if (newHead >= stackSize)
                {
                    growFreeStack();
                    oldHead = head.load(std::memory_order_acquire);
                    continue;
                }
                freeStack[oldHead] = i;
                if (head.compare_exchange_weak(oldHead, newHead, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
                {
                    break;
                }
            }
        }

        // 扩展 slots
        slots.resize(newSize);

        // 更新 size
        size.store(newSize, std::memory_order_release);

        // 获取一个空闲槽位
        return popFreeSlot();
    }

    void growFreeStack()
    {
        size_t oldSize = freeStackSize.load(std::memory_order_acquire);
        size_t newSize = oldSize * 2 + 1024;
        freeStack.resize(newSize);
        freeStackSize.store(newSize, std::memory_order_release);
    }
};
}; // namespace TJS
