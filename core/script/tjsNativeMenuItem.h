#pragma once

#include "TVPEvent.h"

#include "tjsNative.h"
#include "PlatformMutex.h"

template<typename T>
struct ObjectVector : public std::vector<T*>
{ // thread safe vector
    tTJSCriticalSection Lock;

    tjs_int Find(const T* object) const
    {
        tTJSCriticalSectionHolder holder(((ObjectVector*)this)->Lock);
        auto it = std::find(this->begin(), this->end(), object);
        if (it == this->end())
            return -1;
        return it - this->begin();
    }

    bool Add(T* object, tjs_int idx = -1)
    {
        tTJSCriticalSectionHolder holder(Lock);
        if (std::find(this->begin(), this->end(), object) != this->end())
            return false;
        if (idx == -1)
            this->push_back(object);
        else
            this->insert(this->begin() + idx, object);
        return true;
    }

    bool Remove(T* object)
    {
        tTJSCriticalSectionHolder holder(Lock);
        auto it = std::find(this->begin(), this->end(), object);
        if (it == this->end())
            return false;
        this->erase(it);
        return true;
    }
};

//---------------------------------------------------------------------------
// tTJSNI_MenuItem
//---------------------------------------------------------------------------
class tTJSNI_Window;
class tTJSNI_MenuItem;
class tTJSNI_BaseMenuItem : public tTJSNativeInstance
{
    typedef tTJSNativeInstance inherited;
    friend class tTJSNI_MenuItem;

protected:
    ObjectVector<tTJSNI_BaseMenuItem> Children;
    tTJSNI_Window* Window;

    iTJSDispatch2* Owner;
    tTJSVariantClosure ActionOwner; // object to send action

    tTJSNI_BaseMenuItem* Parent;

    bool ChildrenArrayValid;
    iTJSDispatch2* ChildrenArray;
    iTJSDispatch2* ArrayClearMethod;

public:
    tTJSNI_BaseMenuItem();
    tjs_error Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj);
    void Invalidate();

public:
    static tTJSNI_MenuItem* CastFromVariant(const tTJSVariant& from);

protected:
    virtual bool CanDeliverEvents() const = 0; // must be implemented in each platforms

protected:
    void AddChild(tTJSNI_BaseMenuItem* item);
    void RemoveChild(tTJSNI_BaseMenuItem* item);

public:
    virtual void Add(tTJSNI_MenuItem* item) = 0;
    virtual void Insert(tTJSNI_MenuItem* item, tjs_int index) = 0;
    virtual void Remove(tTJSNI_MenuItem* item) = 0;

public:
    tTJSVariantClosure GetActionOwnerNoAddRef() const { return ActionOwner; }

    iTJSDispatch2* GetOwnerNoAddRef() const { return Owner; }

    tTJSNI_BaseMenuItem* GetParent() const { return Parent; }

    tTJSNI_BaseMenuItem* GetRootMenuItem() const;

    tTJSNI_Window* GetWindow() const { return Window; }

    iTJSDispatch2* GetChildrenArrayNoAddRef();

public:
    void OnClick(void); // fire onClick event
};
//---------------------------------------------------------------------------
iTJSDispatch2* TVPGetMenuDispatch(TVPWindow* hWnd);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_MenuItem : MenuItem Native Instance
//---------------------------------------------------------------------------
class tTJSNI_MenuItem : public tTJSNI_BaseMenuItem
{
    typedef tTJSNI_BaseMenuItem inherited;

    // TMenuItem* MenuItem;

    ttstr Caption;
    ttstr Shortcut;
    bool IsAttched;
    bool IsChecked;
    bool IsEnabled;
    bool IsRadio;
    bool IsVisible;

    tjs_int GroupIndex;

public:
    tTJSNI_MenuItem();
    tjs_error Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj);
    void Invalidate();

private:
    void MenuItemClick();

protected:
    bool CanDeliverEvents() const;
    // tTJSNI_BaseMenuItem::CanDeliverEvents override

public:
    void Add(tTJSNI_MenuItem* item);
    void Insert(tTJSNI_MenuItem* item, tjs_int index);
    void Remove(tTJSNI_MenuItem* item);

    void SetCaption(const ttstr& caption);
    void GetCaption(ttstr& caption) const;

    void SetChecked(bool b);
    bool GetChecked() const;

    void SetEnabled(bool b);
    bool GetEnabled() const;

    void SetGroup(tjs_int g);
    tjs_int GetGroup() const;

    void SetRadio(bool b);
    bool GetRadio() const;

    void SetShortcut(const ttstr& shortcut);
    void GetShortcut(ttstr& shortcut) const;

    void SetVisible(bool b);
    bool GetVisible() const;

    tjs_int GetIndex() const;
    void SetIndex(tjs_int newIndex);

    tjs_int TrackPopup(tjs_uint32 flags, tjs_int x, tjs_int y) const;

    const ObjectVector<tTJSNI_BaseMenuItem>& GetChildren() const { return Children; }

    //-- interface to plugin
    /*HMENU*/ void* GetMenuItemHandleForPlugin() const;
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
extern iTJSDispatch2* TVPCreateMenuItemObject(iTJSDispatch2* window);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPMenuItemOnClickInputEvent : onClick input event class
//---------------------------------------------------------------------------
class tTVPOnMenuItemClickInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnMenuItemClickInputEvent(tTJSNI_BaseMenuItem* menu) : tTVPBaseInputEvent(menu, Tag){};
    void Deliver() const { ((tTJSNI_BaseMenuItem*)GetSource())->OnClick(); }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_MenuItem : TJS MenuItem class
//---------------------------------------------------------------------------
class tTJSNC_MenuItem : public tTJSNativeClass
{
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_MenuItem();
    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance();
    /*
            implement this in each platform.
            this must return a proper instance of tTJSNC_MenuItem.
    */
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_MenuItem();
/*
        implement this in each platform.
        this must return a proper instance of tTJSNC_MenuItem.
        usually simple returns: new tTJSNC_MenuItem();
*/
//---------------------------------------------------------------------------
