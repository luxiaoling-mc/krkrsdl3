#include "tjsCommHead.h"
#include "tjsNativeMenuItem.h"

#include "TVPScript.h"
#include "TVPMsg.h"
#include "TVPSystem.h"
#include "TVPWindow.h"
#include "tjsDictionary.h"
#include "tjsArray.h"
#include "tjsNativeWindow.h"
#include "WindowManager.h"
#include "PlatformMutex.h"
#include <map>

//---------------------------------------------------------------------------
// tTJSNI_MenuItem
//---------------------------------------------------------------------------
tTJSNI_MenuItem::tTJSNI_MenuItem()
{
    IsChecked = false;
    IsAttched = true;
    IsEnabled = true;
    IsRadio = false;
    IsVisible = true;
    GroupIndex = 0;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_MenuItem::Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj)
{
    inherited::Construct(numparams, param, tjs_obj);

    // create or attach MenuItem object
    if (Window)
    {
        // MenuItem = Window->GetRootMenuItem();
        IsAttched = true;
    }
    else
    {
        // 		MenuItem = new TMenuItem();
        // 		MenuItem->OnClick = std::bind(&tTJSNI_MenuItem::MenuItemClick, this);
        IsAttched = false;
    }

    // fetch initial caption
    if (!Window && numparams >= 2)
    {
        Caption = *param[1];
        // MenuItem->setCaption (Caption);
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::Invalidate()
{
    // invalidate inherited
    inherited::Invalidate(); // this sets Owner = NULL

    // delete VCL object
    // if (!IsAttched && MenuItem) delete MenuItem, MenuItem = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::MenuItemClick()
{
    // VCL event handler
    // post to the event queue
    TVPPostInputEvent(new tTVPOnMenuItemClickInputEvent(this));
}
//---------------------------------------------------------------------------
bool tTJSNI_MenuItem::CanDeliverEvents() const
{
    // returns whether events can be delivered
    // if(!MenuItem) return false;
    bool enabled = true;

    const tTJSNI_MenuItem* item = this;
    while (item)
    {
        if (!item->GetEnabled())
        {
            enabled = false;
            break;
        }
        item = static_cast<const tTJSNI_MenuItem*>(item->GetParent());
    }
    return enabled;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::Add(tTJSNI_MenuItem* item)
{
    // 	if(MenuItem && item->MenuItem)
    // 	{
    // 		MenuItem->Add(item->MenuItem);
    AddChild(item);
    //}
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::Insert(tTJSNI_MenuItem* item, tjs_int index)
{
    // 	if(MenuItem && item->MenuItem)
    // 	{
    // 		MenuItem->Insert(index, item->MenuItem);
    if (Children.Add(item, index))
    {
        ChildrenArrayValid = false;
        if (item->Owner)
            item->Owner->AddRef();
        item->Parent = this;
    }
    // AddChild(item);
    //}
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::Remove(tTJSNI_MenuItem* item)
{
    // 	if(MenuItem && item->MenuItem)
    // 	{
    // 		int index = MenuItem->IndexOf(item->MenuItem);
    // 		if(index == -1) TVPThrowExceptionMessage(TVPNotChildMenuItem);
    //
    // 		MenuItem->Delete(index);
    RemoveChild(item);
    //}
}
//---------------------------------------------------------------------------
void* tTJSNI_MenuItem::GetMenuItemHandleForPlugin() const
{
    /*if(!MenuItem)*/ return NULL;
    // return MenuItem->getHandle();
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_MenuItem::GetIndex() const
{
    if (!Parent)
        return 0;
    // if(!MenuItem) return 0;
    // return MenuItem->getMenuIndex();
    tjs_int idx = Parent->Children.Find((tTJSNI_BaseMenuItem*)this);
    if (idx < 0)
        idx = 0;
    return idx;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetIndex(tjs_int newIndex)
{
    // 	if(!MenuItem) return;
    // 	MenuItem->setMenuIndex (newIndex);
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetCaption(const ttstr& caption)
{
    // if(!MenuItem) return;
    Caption = caption;
    // 	MenuItem->setAutoHotkeys (maManual);
    // 	MenuItem->setCaption (caption);
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::GetCaption(ttstr& caption) const
{
    // if(!MenuItem) caption.Clear();
    caption = Caption;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetChecked(bool b)
{
    // if(!MenuItem) return;
    // MenuItem->setChecked (b);
    if (b && IsRadio && Parent)
    {
        for (tTJSNI_BaseMenuItem* _item : Parent->Children)
        {
            tTJSNI_MenuItem* item = static_cast<tTJSNI_MenuItem*>(_item);
            if (item->IsRadio && item->GroupIndex == GroupIndex)
                item->IsChecked = false;
        }
    }
    IsChecked = b;
}
//---------------------------------------------------------------------------
bool tTJSNI_MenuItem::GetChecked() const
{
    // 	if(!MenuItem) return false;
    // 	return MenuItem->getChecked();
    return IsChecked;
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetEnabled(bool b)
{
    IsEnabled = b;
    // 	if(!MenuItem) return;
    // 	MenuItem->setEnabled (b);
}
//---------------------------------------------------------------------------
bool tTJSNI_MenuItem::GetEnabled() const
{
    return IsEnabled;
    // 	if(!MenuItem) return false;
    // 	return MenuItem->getEnabled();
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetGroup(tjs_int g)
{
    GroupIndex = g;
    // 	if(!MenuItem) return;
    // 	MenuItem->setGroupIndex ((BYTE)g);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_MenuItem::GetGroup() const
{
    return GroupIndex;
    // 	if(!MenuItem) return 0;
    // 	return MenuItem->getGroupIndex();
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetRadio(bool b)
{
    IsRadio = b;
    // 	if(!MenuItem) return;
    // 	MenuItem->setRadioItem (b);
}
//---------------------------------------------------------------------------
bool tTJSNI_MenuItem::GetRadio() const
{
    return IsRadio;
    // 	if(!MenuItem) return false;
    // 	return MenuItem->getRadioItem();
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetShortcut(const ttstr& shortcut)
{
    // if(!MenuItem) return;
    //	MenuItem->setShortCut (TextToShortCut(shortcut.AsAnsiString()));
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::GetShortcut(ttstr& shortcut) const
{
    /*if(!MenuItem)*/ shortcut.Clear();
    //	shortcut = ShortCutToText(MenuItem->getShortCut());
}
//---------------------------------------------------------------------------
void tTJSNI_MenuItem::SetVisible(bool b)
{
    IsVisible = b;
    // 	if(!MenuItem) return;
    // 	if(Window) Window->SetMenuBarVisible(b); else MenuItem->setVisible (b);
}
//---------------------------------------------------------------------------
bool tTJSNI_MenuItem::GetVisible() const
{
    return IsVisible;
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_MenuItem::TrackPopup(tjs_uint32 flags, tjs_int x, tjs_int y) const
{
    return 1;
}

//---------------------------------------------------------------------------
// Input Events
//---------------------------------------------------------------------------
// For input event tag
tTVPUniqueTagForInputEvent tTVPOnMenuItemClickInputEvent ::Tag;
//---------------------------------------------------------------------------

static const tjs_char* TVPSpecifyMenuItem = TJS_N("Please specity MenuItem class object.");

//---------------------------------------------------------------------------
// tTJSNI_BaseMenuItem
//---------------------------------------------------------------------------
tTJSNI_BaseMenuItem::tTJSNI_BaseMenuItem()
{
    Owner = NULL;
    Window = NULL;
    Parent = NULL;
    ChildrenArrayValid = false;
    ChildrenArray = NULL;
    ArrayClearMethod = NULL;

    ActionOwner.Object = ActionOwner.ObjThis = NULL;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseMenuItem::Construct(tjs_int numparams,
                                         tTJSVariant** param,
                                         iTJSDispatch2* tjs_obj)
{
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    ActionOwner = param[0]->AsObjectClosure();
    Owner = tjs_obj;

    if (numparams >= 2)
    {
        if (param[1]->Type() == tvtObject)
        {
            // is this Window instance ?
            tTJSVariantClosure clo = param[1]->AsObjectClosureNoAddRef();
            if (clo.Object == NULL)
                TVPThrowExceptionMessage(TVPSpecifyWindow);
            tTJSNI_Window* win = NULL;
            if (TJS_FAILED(clo.Object->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, (iTJSNativeInstance**)&win)))
                TVPThrowExceptionMessage(TVPSpecifyWindow);
            Window = win;
        }
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseMenuItem::Invalidate()
{
    TVPCancelSourceEvents(Owner);
    TVPCancelInputEvents(this);

    { // locked
        tTJSCriticalSectionHolder holder(Children.Lock);
        tjs_int count = Children.size();
        for (tjs_int i = 0; i < count; i++)
        {
            tTJSNI_BaseMenuItem* item = Children.at(i);
            if (!item)
                continue;

            if (item->Owner)
            {
                item->Owner->Invalidate(0, NULL, NULL, item->Owner);
                item->Owner->Release();
            }
        }
        Children.clear();
    } // locked

    //	Owner = NULL;
    Window = NULL;
    Parent = NULL;

    if (ChildrenArray)
        ChildrenArray->Release(), ChildrenArray = NULL;
    if (ArrayClearMethod)
        ArrayClearMethod->Release(), ArrayClearMethod = NULL;

    ActionOwner.Release();
    ActionOwner.ObjThis = ActionOwner.Object = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
tTJSNI_MenuItem* tTJSNI_BaseMenuItem::CastFromVariant(const tTJSVariant& from)
{
    if (from.Type() == tvtObject)
    {
        // is this Window instance ?
        tTJSVariantClosure clo = from.AsObjectClosureNoAddRef();
        if (clo.Object == NULL)
            TVPThrowExceptionMessage(TVPSpecifyMenuItem);
        tTJSNI_MenuItem* menuitem = NULL;
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, tTJSNC_MenuItem::ClassID, (iTJSNativeInstance**)&menuitem)))
            TVPThrowExceptionMessage(TVPSpecifyMenuItem);
        return menuitem;
    }
    TVPThrowExceptionMessage(TVPSpecifyMenuItem);
    return NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseMenuItem::AddChild(tTJSNI_BaseMenuItem* item)
{
    if (Children.Add(item))
    {
        ChildrenArrayValid = false;
        if (item->Owner)
            item->Owner->AddRef();
        item->Parent = this;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseMenuItem::RemoveChild(tTJSNI_BaseMenuItem* item)
{
    if (Children.Remove(item))
    {
        ChildrenArrayValid = false;
        if (item->Owner)
            item->Owner->Release();
        item->Parent = NULL;
    }
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_BaseMenuItem::GetChildrenArrayNoAddRef()
{
    if (!ChildrenArray)
    {
        // create an Array object
        iTJSDispatch2* classobj;
        ChildrenArray = TJSCreateArrayObject(&classobj);
        try
        {
            tTJSVariant val;
            tjs_error er;
            er = classobj->PropGet(0, TJS_N("clear"), NULL, &val, classobj);
            // retrieve clear method
            if (TJS_FAILED(er))
                TVPThrowInternalError;
            ArrayClearMethod = val.AsObject();
        }
        catch (...)
        {
            ChildrenArray->Release();
            ChildrenArray = NULL;
            classobj->Release();
            throw;
        }
        classobj->Release();
    }

    if (!ChildrenArrayValid)
    {
        // re-create children list
        ArrayClearMethod->FuncCall(0, NULL, NULL, NULL, 0, NULL, ChildrenArray);
        // clear array

        { // locked
            tTJSCriticalSectionHolder holder(Children.Lock);
            tjs_int count = Children.size();
            tjs_int itemcount = 0;
            for (tjs_int i = 0; i < count; i++)
            {
                tTJSNI_BaseMenuItem* item = Children.at(i);
                if (!item)
                    continue;

                iTJSDispatch2* dsp = item->Owner;
                tTJSVariant val(dsp, dsp);
                ChildrenArray->PropSetByNum(TJS_MEMBERENSURE, itemcount, &val, ChildrenArray);
                itemcount++;
            }
        } // locked

        ChildrenArrayValid = true;
    }

    return ChildrenArray;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseMenuItem::OnClick(void)
{
    // fire onClick event
    if (!CanDeliverEvents())
        return;

    // also check window
    tTJSNI_BaseMenuItem* item = this;
    while (!item->Window)
    {
        if (!item->Parent)
            break;
        item = item->Parent;
    }
    if (!item->Window)
        return;
    if (!item->Window->CanDeliverEvents())
        return;

    // fire event
    static ttstr eventname(TJS_N("onClick"));
    TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    TVPDoSaveSystemVariables();
}
//---------------------------------------------------------------------------
tTJSNI_BaseMenuItem* tTJSNI_BaseMenuItem::GetRootMenuItem() const
{
    tTJSNI_BaseMenuItem* current = const_cast<tTJSNI_BaseMenuItem*>(this);
    tTJSNI_BaseMenuItem* parent = current->GetParent();
    while (parent)
    {
        current = parent;
        parent = current->GetParent();
    }
    return current;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static tTJSNativeClass* _menuitemclass = NULL;
static iTJSDispatch2* textToKeycodeMap = nullptr;
static iTJSDispatch2* keycodeToTextList = nullptr;
iTJSDispatch2* TVPCreateMenuItemObject(iTJSDispatch2* window)
{
    if (!_menuitemclass)
    {
        _menuitemclass = new tTJSNC_MenuItem();
        TJS_REGISTER_STATIC_HEAP(_menuitemclass);
        TJS_REGISTER_STATIC_HEAP(textToKeycodeMap);
        TJS_REGISTER_STATIC_HEAP(textToKeycodeMap);
    }

    iTJSDispatch2* out;
    tTJSVariant param(window);
    tTJSVariant* pparam[2] = {&param, &param};
    if (TJS_FAILED(_menuitemclass->CreateNew(0, NULL, NULL, &out, 2, pparam, _menuitemclass)))
        TVPThrowExceptionMessage(TVPInternalError, TJS_N("TVPCreateMenuItemObject"));

    return out;
}
//---------------------------------------------------------------------------

static std::map<tTVInteger, iTJSDispatch2*> MENU_LIST;
static void AddMenuDispatch(tTVInteger hWnd, iTJSDispatch2* menu)
{
    if (MENU_LIST.size() == 0)
        TJSAddStaticToRegisterHeap([](void*) { MENU_LIST.clear(); }, NULL);
    MENU_LIST.insert(std::map<tTVInteger, iTJSDispatch2*>::value_type(hWnd, menu));
}
iTJSDispatch2* TVPGetMenuDispatch(tTVInteger hWnd)
{
    std::map<tTVInteger, iTJSDispatch2*>::iterator i = MENU_LIST.find(hWnd);
    if (i != MENU_LIST.end())
    {
        return i->second;
    }
    return NULL;
}
static bool _IsWindow(tTVInteger hWnd)
{
    tjs_int count = TVPGetWindowCount();
    for (tjs_int i = 0; i < count; ++i)
    {
        if (TVPGetWindowListAt(i) == (TVPWindow*)(hWnd))
            return true;
    }
    return false;
}

static void UpdateMenuList()
{
    std::map<tTVInteger, iTJSDispatch2*>::iterator i = MENU_LIST.begin();
    for (; i != MENU_LIST.end();)
    {
        tTVInteger hWnd = i->first;
        bool exist = _IsWindow(hWnd);
        if (exist == false)
        {
            // 既になくなったWindow
            std::map<tTVInteger, iTJSDispatch2*>::iterator target = i;
            i++;
            iTJSDispatch2* menu = target->second;
            MENU_LIST.erase(target);
            menu->Release();
            // TVPDeleteAcceleratorKeyTable(hWnd);
        }
        else
        {
            i++;
        }
    }
}

class WindowMenuProperty
  : public tTJSDispatch{tjs_error PropGet(tjs_uint32 flag,
                                          const tjs_char* membername,
                                          tjs_uint32* hint,
                                          tTJSVariant* result,
                                          iTJSDispatch2* objthis){tTJSVariant var;
if (TJS_FAILED(objthis->PropGet(0, TJS_N("HWND"), NULL, &var, objthis)))
{
    return TJS_E_INVALIDOBJECT;
}
tTVInteger hWnd = var.AsInteger();
iTJSDispatch2* menu = TVPGetMenuDispatch(hWnd);
if (menu == NULL)
{
    UpdateMenuList();
    menu = TVPCreateMenuItemObject(objthis);
    AddMenuDispatch(hWnd, menu);
}
*result = tTJSVariant(menu, menu);
return TJS_S_OK;
}
tjs_error PropSet(tjs_uint32 flag,
                  const tjs_char* membername,
                  tjs_uint32* hint,
                  const tTJSVariant* param,
                  iTJSDispatch2* objthis)
{
    return TJS_E_ACCESSDENYED;
}
}
*gWindowMenuProperty;

static bool SetShortCutKeyCode(ttstr text, int key, bool force)
{
    tTJSVariant vtext(text);
    tTJSVariant vkey(key);

    text.ToLowerCase();
    if (TJS_FAILED(textToKeycodeMap->PropSet(TJS_MEMBERENSURE, text.c_str(), nullptr, &vkey,
                                             textToKeycodeMap)))
        return false;
    if (force == false)
    {
        tTJSVariant var;
        keycodeToTextList->PropGetByNum(0, key, &var, keycodeToTextList);
        if (var.Type() == tvtString)
            return true;
    }
    return TJS_SUCCEEDED(
        keycodeToTextList->PropSetByNum(TJS_MEMBERENSURE, key, &vtext, keycodeToTextList));
}

static void CreateShortCutKeyCodeTable()
{
    textToKeycodeMap = TJSCreateDictionaryObject();
    keycodeToTextList = TJSCreateArrayObject();
    if (textToKeycodeMap == nullptr || keycodeToTextList == nullptr)
        return;

    // 吉里吉里２互換用ショートカット文字列
    SetShortCutKeyCode(TJS_N("BkSp"), VK_BACK, false);
    SetShortCutKeyCode(TJS_N("PgUp"), VK_PRIOR, false);
    SetShortCutKeyCode(TJS_N("PgDn"), VK_NEXT, false);
}

//---------------------------------------------------------------------------
// tTJSNC_MenuItem::CreateNativeInstance
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_MenuItem::CreateNativeInstance()
{
    return new tTJSNI_MenuItem();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_MenuItem
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_MenuItem()
{
    tTJSNativeClass* cls = new tTJSNC_MenuItem();
    static tjs_uint32 TJS_NCM_CLASSID;
    TJS_NCM_CLASSID = tTJSNC_MenuItem::ClassID;

    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(HMENU){TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    if (result)
        *result = (tTVInteger)(void*)_this->GetMenuItemHandleForPlugin();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, HMENU)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(textToKeycode){TJS_BEGIN_NATIVE_PROP_GETTER{
    if (result) * result = tTJSVariant(textToKeycodeMap, textToKeycodeMap);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, textToKeycode)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(keycodeToText){TJS_BEGIN_NATIVE_PROP_GETTER{
    if (result) * result = tTJSVariant(keycodeToTextList, keycodeToTextList);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, keycodeToText)
//---------------------------------------------------------------------------

if (!textToKeycodeMap)
{
    CreateShortCutKeyCodeTable();

    tTJSVariant val;

    iTJSDispatch2* global = TVPGetScriptDispatch();

    {
        gWindowMenuProperty = new WindowMenuProperty();
        val = tTJSVariant(gWindowMenuProperty);
        gWindowMenuProperty->Release();
        tTJSVariant win;
        if (TJS_SUCCEEDED(global->PropGet(0, TJS_N("Window"), NULL, &win, global)))
        {
            iTJSDispatch2* obj = win.AsObjectNoAddRef();
            obj->PropSet(TJS_MEMBERENSURE, TJS_N("menu"), NULL, &val, obj);
            win.Clear();
        }
        val.Clear();

        //-----------------------------------------------------------------------
        iTJSDispatch2* tjsclass = TVPCreateNativeClass_MenuItem();
        val = tTJSVariant(tjsclass);
        tjsclass->Release();
        global->PropSet(TJS_MEMBERENSURE, TJS_N("MenuItem"), NULL, &val, global);
        //-----------------------------------------------------------------------
    }

    global->Release();
}

return cls;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_MenuItem
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_MenuItem::ClassID = -1;
tTJSNC_MenuItem::tTJSNC_MenuItem() : inherited(TJS_N("MenuItem"))
{
    // registration of native members

    TJS_BEGIN_NATIVE_MEMBERS(MenuItem) // constructor
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this, /*var.type*/ tTJSNI_MenuItem,
                                      /*TJS class name*/ MenuItem)
    {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ MenuItem)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ add)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        tTJSNI_MenuItem* item = tTJSNI_BaseMenuItem::CastFromVariant(*param[0]);
        _this->Add(item);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ add)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ insert)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
        if (numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        tTJSNI_MenuItem* item = tTJSNI_BaseMenuItem::CastFromVariant(*param[0]);
        tjs_int index = *param[1];
        _this->Insert(item, index);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ insert)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ remove)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        tTJSNI_MenuItem* item = tTJSNI_BaseMenuItem::CastFromVariant(*param[0]);
        _this->Remove(item);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ remove)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ popup) // not trackPopup
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
        if (numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        tjs_uint32 flags = (tTVInteger)*param[0];
        tjs_int x = *param[1];
        tjs_int y = *param[2];
        tjs_int rv = _this->TrackPopup(flags, x, y);
        if (result)
            *result = rv;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ popup) // not trackPopup
    //---------------------------------------------------------------------------

    //-- events

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onClick)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_MenuItem);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if (obj.Object)
        {
            TVP_ACTION_INVOKE_BEGIN(0, "onClick", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onClick)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ fireClick)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_MenuItem);

        _this->OnClick();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ fireClick)
    //----------------------------------------------------------------------

    //--properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(caption){TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    ttstr res;
    _this->GetCaption(res);
    *result = res;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetCaption(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(caption)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(checked){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
*result = _this->GetChecked();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetChecked(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(checked)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enabled){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
*result = _this->GetEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetEnabled(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(group){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
*result = _this->GetGroup();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetGroup((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(group)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(radio){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
*result = _this->GetRadio();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetRadio(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(radio)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(shortcut){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
ttstr res;
_this->GetShortcut(res);
*result = res;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetShortcut(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(shortcut)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(visible){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
*result = _this->GetVisible();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetVisible(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(visible)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(parent){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
tTJSNI_BaseMenuItem* parent = _this->GetParent();
if (parent)
{
    iTJSDispatch2* dsp = parent->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
}
else
{
    *result = tTJSVariant((iTJSDispatch2*)NULL);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(parent)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(children){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
iTJSDispatch2* dsp = _this->GetChildrenArrayNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(children)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(root) // not rootMenuItem
{TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
tTJSNI_BaseMenuItem* root = _this->GetRootMenuItem();
if (root)
{
    iTJSDispatch2* dsp = root->GetOwnerNoAddRef();
    if (result)
        *result = tTJSVariant(dsp, dsp);
}
else
{
    if (result)
        *result = tTJSVariant((iTJSDispatch2*)NULL);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(root) // not rootMenuItem
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(window){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
tTJSNI_Window* window = _this->GetWindow();
if (window)
{
    iTJSDispatch2* dsp = window->GetOwnerNoAddRef();
    if (result)
        *result = tTJSVariant(dsp, dsp);
}
else
{
    if (result)
        *result = tTJSVariant((iTJSDispatch2*)NULL);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(window)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(index){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
if (result)
    *result = (tTVInteger)_this->GetIndex();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_MenuItem);
    _this->SetIndex((tjs_int)(*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(index)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
