//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include <functional>
#include "TVPScript.h"
#include "TVPPlugin.h"
#include "TVPStorage.h"
#include "TVPGraphicsLoader.h"

#include "TVPMsg.h"
#include "TVPSystem.h"

#include "tjsHashSearch.h"
#include "TVPEvent.h"
#include "TVPTrans.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "TVPDebug.h"
#include "PlatformFile.h"
#include "tjs.h"
#include "FuncStubs.h"

#include "TVPApplication.h"

//---------------------------------------------------------------------------
bool TVPLoadInternalPlugin(const ttstr& _name);
extern std::set<ttstr> TVPRegisteredPlugins;
static bool TVPPluginLoading = false;
void TVPLoadPlugin(const ttstr& name)
{
    bool success = TVPLoadInternalPlugin(name);
    return; // seal all plugins
}
//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr& name)
{
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// plug-in autoload support
//---------------------------------------------------------------------------
struct tTVPFoundPlugin
{
    std::string Path;
    std::string Name;
    bool operator<(const tTVPFoundPlugin& rhs) const { return Name < rhs.Name; }
};
static tjs_int TVPAutoLoadPluginCount = 0;
static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin>& list, const ttstr& folder)
{
    std::string folderName = TVPGetLocallyAccessibleName(folder).AsStdString();
    TVPListDir(folderName,
               [&](const std::string& filename, int mask)
               {
                   if (mask & S_IFREG)
                   {
                       if (!TJS_strcasecmp(filename.c_str() + filename.length() - 4, ".tpm"))
                       {
                           tTVPFoundPlugin fp;
                           fp.Path = folderName;
                           fp.Name = filename;
                           list.emplace_back(fp);
                       }
                   }
               });
}

void TVPLoadInternalPlugins();
void TVPLoadPluigins(void)
{
    TVPLoadInternalPlugins();
    // This function searches plugins which have an extension of ".tpm"
    // in the default path:
    //    1. a folder which holds kirikiri executable
    //    2. "plugin" folder of it
    // Plugin load order is to be decided using its name;
    // aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

    // search plugins from path: (exepath), (exepath)\system, (exepath)\plugin
    std::vector<tTVPFoundPlugin> list;

    TVPSearchPluginsAt(list, TVPProjectDir);
    TVPSearchPluginsAt(list, TVPProjectDir + "system");
    TVPSearchPluginsAt(list, TVPProjectDir + "plugin");

    // sort by filename
    std::sort(list.begin(), list.end());

    // load each plugin
    TVPAutoLoadPluginCount = (tjs_int)list.size();
    for (std::vector<tTVPFoundPlugin>::iterator i = list.begin(); i != list.end(); i++)
    {
        TVPAddImportantLog(ttstr(TJS_N("(info) Loading ")) + ttstr(i->Name.c_str()));
        TVPLoadPlugin((i->Path + "/" + i->Name).c_str());
    }
}
//---------------------------------------------------------------------------
void TVPUnloadInternalPlugins();
void TVPUnloadPlugins()
{
    TVPUnloadInternalPlugins();
}
//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount()
{
    return TVPAutoLoadPluginCount;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char* name, iTJSDispatch2* dsp)
{
    // register given object to global object
    tTJSVariant val(dsp);
    iTJSDispatch2* global = TVPGetScriptDispatch();
    tjs_error er;
    try
    {
        er = global->PropSet(TJS_MEMBERENSURE, name, NULL, &val, global);
    }
    catch (...)
    {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char* name)
{
    // remove registration of global object
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global)
        return false;
    tjs_error er;
    try
    {
        er = global->DeleteMember(0, name, NULL, global);
    }
    catch (...)
    {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                   tTVPCatchBlockFunction catchblock,
                   tTVPFinallyBlockFunction finallyblock,
                   void* data)
{
    try
    {
        tryblock(data);
    }
    catch (const eTJS& e)
    {
        if (finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_N("eTJS");
        desc.message = e.GetMessage();
        if (catchblock(data, desc))
            throw;
        return;
    }
    catch (...)
    {
        if (finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_N("unknown");
        if (catchblock(data, desc))
            throw;
        return;
    }
    if (finallyblock)
        finallyblock(data);
}
//---------------------------------------------------------------------------

#include "ncbind/ncbind.hpp"

void TVPLoadInternalPlugins()
{
    // 如果插件不冲突，实际上可以加载全部
    ncbAutoRegister::LoadModule(TJS_N("kirikiroid2.dll"));
    ncbAutoRegister::LoadModule(TJS_N("xp3filter.dll"));
    ncbAutoRegister::LoadModule(TJS_N("wuffmpeg.dll"));
    ncbAutoRegister::LoadModule(TJS_N("wuopus.dll"));
    ncbAutoRegister::LoadModule(TJS_N("wuvorbis.dll"));
    ncbAutoRegister::LoadModule(TJS_N("win32dialog.dll"));
    ncbAutoRegister::LoadModule(TJS_N("varfile.dll"));
    ncbAutoRegister::LoadModule(TJS_N("shrinkCopy.dll"));
}

void TVPUnloadInternalPlugins()
{
    ncbAutoRegister::ClearRegisteredModule();
}

bool TVPLoadInternalPlugin(const ttstr& _name)
{
    return ncbAutoRegister::LoadModule(TVPExtractStorageName(_name));
}