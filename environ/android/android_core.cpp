#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "WindowManager.h"
#include "tjsNativeMenuItem.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/system_properties.h>

#include <SDL3/SDL_system.h>
#include <SDL3/SDL_events.h>

//---------------------------------------------------------------------------
static AAssetManager* mgr = NULL;
extern "C" JNIEXPORT void JNICALL Java_org_tvp_krkrsdl3_KRKRActivity_setNativeAssetManager(
    JNIEnv* env, jobject instance, jobject assetManager)
{
    mgr = AAssetManager_fromJava(env, assetManager);
}
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    AAsset* assetFile = AAssetManager_open(mgr, filename.AsStdString().c_str(), AASSET_MODE_BUFFER);
    if (assetFile)
    {
        size_t fileLength = AAsset_getLength(assetFile);
        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, fileLength);
        AAsset_read(assetFile, ret->GetInternalBuffer(), fileLength);
        AAsset_close(assetFile);
        return ret;
    }
    return nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string TVPGetPackageVersionString()
{
    return "android";
}
ttstr TVPGetOSName()
{
    // Android版本检测
    char sdk_ver[PROP_VALUE_MAX];
    char release[PROP_VALUE_MAX];

    __system_property_get("ro.build.version.sdk", sdk_ver);
    __system_property_get("ro.build.version.release", release);

    std::string version = release;

    // 添加Android版本名称
    int sdk = atoi(sdk_ver);
    switch (sdk)
    {
        case 34:
            version += " (14)";
            break;
        case 33:
            version += " (13)";
            break;
        case 32:
            version += " (12L)";
            break;
        case 31:
            version += " (12)";
            break;
        case 30:
            version += " (11)";
            break;
        case 29:
            version += " (10)";
            break;
        case 28:
            version += " (9 Pie)";
            break;
        case 27:
            version += " (8.1 Oreo)";
            break;
        case 26:
            version += " (8.0 Oreo)";
            break;
        case 25:
            version += " (7.1 Nougat)";
            break;
        case 24:
            version += " (7.0 Nougat)";
            break;
        case 23:
            version += " (6.0 Marshmallow)";
            break;
        case 22:
            version += " (5.1 Lollipop)";
            break;
        case 21:
            version += " (5.0 Lollipop)";
            break;
    }

    return "Android " + version;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>& btns)
{
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return -1;

    jclass cls = env->FindClass("org/tvp/krkrsdl3/KRKRCall");
    if (!cls) {
        env->ExceptionClear();
        return -1;
    }

    // 1. 调用 ShowInputBox 显示对话框（不阻塞）
    jmethodID showMethod = env->GetStaticMethodID(cls, "ShowInputBox",
                                                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V");
    if (!showMethod) {
        env->DeleteLocalRef(cls);
        return -1;
    }

    jstring jTitle = env->NewStringUTF(caption.c_str());
    jstring jPrompt = env->NewStringUTF(prompt.c_str());
    jstring jText = env->NewStringUTF(text.c_str());

    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray jButtons = env->NewObjectArray(btns.size(), strCls, NULL);
    for (size_t i = 0; i < btns.size(); i++) {
        jstring jBtn = env->NewStringUTF(btns[i].c_str());
        env->SetObjectArrayElement(jButtons, i, jBtn);
        env->DeleteLocalRef(jBtn);
    }
    env->DeleteLocalRef(strCls);

    env->CallStaticVoidMethod(cls, showMethod, jTitle, jPrompt, jText, jButtons);

    env->DeleteLocalRef(jTitle);
    env->DeleteLocalRef(jPrompt);
    env->DeleteLocalRef(jText);
    env->DeleteLocalRef(jButtons);

    // 2. 调用 WaitInputResult 阻塞等待
    jmethodID waitMethod = env->GetStaticMethodID(cls, "WaitInputResult", "()I");
    if (!waitMethod) {
        env->DeleteLocalRef(cls);
        return -1;
    }

    jint resultCode = env->CallStaticIntMethod(cls, waitMethod);

    // 3. 获取结果文本
    jmethodID getResultMethod = env->GetStaticMethodID(cls, "GetInputResult", "()Ljava/lang/String;");
    if (getResultMethod) {
        jstring jResult = (jstring)env->CallStaticObjectMethod(cls, getResultMethod);
        const char* utf8 = env->GetStringUTFChars(jResult, NULL);
        if (utf8) {
            text = utf8;
            env->ReleaseStringUTFChars(jResult, utf8);
        }
        env->DeleteLocalRef(jResult);
    }

    env->DeleteLocalRef(cls);
    return (int)resultCode;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static tTJSNI_MenuItem *sdl_current_menu = NULL;
#define SDL_EVENT_MENU_CLICK (SDL_EVENT_USER + 1)
#define SDL_EVENT_TYPE_QUIT SDL_EVENT_QUIT
extern "C" JNIEXPORT void JNICALL
Java_org_tvp_krkrsdl3_KRKRCall_nativeOnMenuItemClick(JNIEnv* env, jclass clazz, jint itemId, jstring itemCaption)
{
    if(!sdl_current_menu || itemId >= sdl_current_menu->GetChildren().size()) return;
    tTJSNI_MenuItem* subitm = static_cast<tTJSNI_MenuItem*>(sdl_current_menu->GetChildren().at(itemId));
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MENU_CLICK;
    event.user.data1 = subitm;
    SDL_PushEvent(&event);
}

extern "C" JNIEXPORT void JNICALL
Java_org_tvp_krkrsdl3_KRKRCall_nativeOnMenuDismiss(JNIEnv* env, jclass clazz)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MENU_CLICK;
    event.user.data1 = NULL;
    SDL_PushEvent(&event);
}

void TVPInvokeMenu(int x, int y, void* _menu)
{
    // 获取菜单们
    if(_menu) sdl_current_menu = static_cast<tTJSNI_MenuItem*>(_menu);
    else
    {
        iTJSDispatch2 *menuobj = TVPGetMenuDispatch(TVPGetActiveWindow());
        if (!menuobj) return;
        menuobj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        tTJSNC_MenuItem::ClassID, (iTJSNativeInstance**)&sdl_current_menu);
        if (sdl_current_menu->GetChildren().empty()) return;
    }
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();

    // 构建java数据
    int count = sdl_current_menu->GetChildren().size();
    jclass dataClass = env->FindClass("org/tvp/krkrsdl3/KRKRCall$MenuItemData");
    jmethodID constructor = env->GetMethodID(dataClass, "<init>", "(ILjava/lang/String;)V");
    jobjectArray itemArray = env->NewObjectArray(count, dataClass, NULL);
    for (int i = 0; i < count; i++) {
        tTJSNI_MenuItem *subitem = static_cast<tTJSNI_MenuItem*>(sdl_current_menu->GetChildren().at(i));
        ttstr captions; subitem->GetCaption(captions);
        if (captions.IsEmpty() || captions == TJS_N("+")) continue;
        jstring caption = env->NewStringUTF(captions.c_str());
        jobject jitem = env->NewObject(dataClass, constructor, i, caption);
        jfieldID typeField = env->GetFieldID(dataClass, "type", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        jclass typeClass = env->FindClass("org/tvp/krkrsdl3/KRKRCall$MenuItemType");
        jfieldID typeValue = NULL;
        if(!subitem->GetChildren().empty())
            typeValue = env->GetStaticFieldID(typeClass, "SUBMENU", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        else if(subitem->GetGroup() > 0 || subitem->GetRadio())
            typeValue = env->GetStaticFieldID(typeClass, "NORMAL", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        else if(subitem->GetChecked())
            typeValue = env->GetStaticFieldID(typeClass, "CHECKBOX", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        else if(captions == "-")
            typeValue = env->GetStaticFieldID(typeClass, "SEPARATOR", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        else
            typeValue = env->GetStaticFieldID(typeClass, "NORMAL", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
        jobject typeObj = env->GetStaticObjectField(typeClass, typeValue);
        env->SetObjectField(jitem, typeField, typeObj);
        jfieldID checkedField = env->GetFieldID(dataClass, "checked", "Z");
        env->SetBooleanField(jitem, checkedField, subitem->GetChecked());
        env->SetObjectArrayElement(itemArray, i, jitem);
        env->DeleteLocalRef(caption);
        env->DeleteLocalRef(jitem);
    }

    // 调用系统显示
    jclass cls = env->FindClass("org/tvp/krkrsdl3/KRKRCall");
    jmethodID method = env->GetStaticMethodID(cls, "showDynamicMenu", "(II[Lorg/tvp/krkrsdl3/KRKRCall$MenuItemData;)V");
    env->CallStaticVoidMethod(cls, method, x, y, itemArray);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(dataClass);
    env->DeleteLocalRef(itemArray);

    // 内部事件循环，阻塞等待菜单点击或关闭
    SDL_Event event;
    while (true)
    {
        if (SDL_WaitEventTimeout(&event, 100) == 0)
            continue;

        if (event.type == SDL_EVENT_MENU_CLICK)
        {
            if (event.user.data1)
            {
                tTJSNI_MenuItem* subitm = static_cast<tTJSNI_MenuItem*>(event.user.data1);
                if (!subitm->GetChildren().empty())
                    TVPInvokeMenu(0, 0, subitm);
                else
                    subitm->OnClick();
            }
            break;
        }
        if (event.type == SDL_EVENT_TYPE_QUIT)
        {
            break;
        }
        SDL_PushEvent(&event);
    }
}
//---------------------------------------------------------------------------