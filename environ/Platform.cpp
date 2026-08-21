#include "tjsCommHead.h"
#include "Platform.h"
#include "cpu_types.h"

#include "TVPEvent.h"
#include "TVPDebug.h"
#include "RenderManager.h"

#ifdef _KRKRSDL3_USE_SDL3
#include <SDL3/SDL_cpuinfo.h>
#elif defined(_KRKRSDL3_OHOS)
#include <unistd.h>
#include <sys/sysinfo.h>
#else
#include <SDL_cpuinfo.h>
#endif

#include <cstdio>

//---------------------------------------------------------------------------
void TVPAbortApplication(int code)
{
    exit(code);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#ifndef _KRKRSDL3_ANDROID
#ifndef _KRKRSDL3_EMSCRIPTEN
#if !(defined(_KRKRSDL3_OHOS) || defined(_KRKRSDL3_IOS)) || defined(__x86_64__)
#include <cpuid.h>
#endif
#endif
#endif
#endif
#ifdef _KRKRSDL3_WINDOWS
#include <windows.h>
#endif
static tjs_uint32 TVPCPUType = 0;
static tjs_uint32 TVPCPUFeatures = 0;
static char TVPCPUVendor[16] = {};
static char TVPCPUName[64] = {};
static bool TVPCPUChecked = false;
//---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define TVP_CPU_ARCH_X86
#endif
#ifdef TVP_CPU_ARCH_X86
static void TVPGetCPUId(int leaf, int subleaf, unsigned int regs[4])
{
#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
    __cpuidex((int*)regs, leaf, subleaf);
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
    regs[0] = eax;
    regs[1] = ebx;
    regs[2] = ecx;
    regs[3] = edx;
#endif
}
#endif
static void TVPDetectVendorAndBrand()
{
#ifdef TVP_CPU_ARCH_X86
    unsigned int regs[4] = {};

    // vendor
    TVPGetCPUId(0, 0, regs);
    memcpy(TVPCPUVendor + 0, &regs[1], 4);
    memcpy(TVPCPUVendor + 4, &regs[3], 4);
    memcpy(TVPCPUVendor + 8, &regs[2], 4);
    TVPCPUVendor[12] = '\0';

    // brand
    for (int leaf = 0x80000002; leaf <= 0x80000004; leaf++)
    {
        TVPGetCPUId(leaf, 0, regs);
        memcpy(TVPCPUName + (leaf - 0x80000002) * 16, regs, 16);
    }
    TVPCPUName[63] = '\0';
    // trim
    for (int i = 63; i >= 0; i--)
    {
        if (TVPCPUName[i] == ' ')
            TVPCPUName[i] = '\0';
        else if (TVPCPUName[i])
            break;
    }

    // vendor constant
    if (std::strcmp(TVPCPUVendor, "GenuineIntel") == 0)
        TVPCPUType |= TVP_CPU_IS_INTEL;
    else if (std::strcmp(TVPCPUVendor, "AuthenticAMD") == 0)
        TVPCPUType |= TVP_CPU_IS_AMD;
    else
        TVPCPUType |= TVP_CPU_IS_UNKNOWN;

    // CPU family
    TVPGetCPUId(1, 0, regs);
    unsigned int family = (regs[0] >> 8) & 0x0f;
    if (family == 0x0f)
        family = ((regs[0] >> 20) & 0xff) + 0x0f;
    TVPCPUType |= (family << 8);

#if defined(_M_X64) || defined(__x86_64__)
    TVPCPUType |= TVP_CPU_FAMILY_X64;
#else
    TVPCPUType |= TVP_CPU_FAMILY_X86;
#endif
#else
    // ARM
    TVPCPUType |= TVP_CPU_FAMILY_ARM;
#if defined(__aarch64__) || defined(_M_ARM64)
    std::strcpy(TVPCPUVendor, "ARM64");
#else
    std::strcpy(TVPCPUVendor, "ARM");
#endif
    std::strcpy(TVPCPUName, TVPCPUVendor);
#if defined(_KRKRSDL3_WINDOWS)
    // Read CPU name from registry (ARM Windows)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                       L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                       0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        WCHAR wbuf[64] = {};
        DWORD type = 0, size = sizeof(wbuf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, &type,
                             (LPBYTE)wbuf, &size) == ERROR_SUCCESS && type == REG_SZ)
        {
            // Convert UTF-16 to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
            if (len > 0 && len <= 64)
            {
                WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, TVPCPUName, 64, NULL, NULL);
                TVPCPUName[63] = '\0';
            }
        }
        RegCloseKey(hKey);
    }
#else
    // Try to get detailed CPU name from /proc/cpuinfo (Linux/Android)
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp)
    {
        char line[256];
        const char* keys[] = {"Hardware", "model name", "Processor"};
        while (fgets(line, sizeof(line), fp))
        {
            for (int k = 0; k < 3; k++)
            {
                size_t klen = std::strlen(keys[k]);
                if (std::strncmp(line, keys[k], klen) == 0 && line[klen] == ':')
                {
                    const char* val = line + klen + 1;
                    while (*val == ' ' || *val == '\t')
                        val++;
                    char* end = TVPCPUName + std::strlen(TVPCPUName);
                    if (end < TVPCPUName + 60)
                        *end++ = ' ';
                    std::strncpy(end, val, TVPCPUName + 63 - end);
                    TVPCPUName[63] = '\0';
                    size_t nlen = std::strlen(TVPCPUName);
                    while (nlen > 0 && (TVPCPUName[nlen - 1] == '\n' || TVPCPUName[nlen - 1] == '\r'))
                        TVPCPUName[--nlen] = '\0';
                    break;
                }
            }
        }
        fclose(fp);
    }
#endif
#endif
}
//---------------------------------------------------------------------------
void TVPDetectCPU()
{
    if (TVPCPUChecked)
        return;
    TVPCPUChecked = true;

    TVPDetectVendorAndBrand();

#ifdef _KRKRSDL3_OHOS
    TVPCPUFeatures |= TVP_CPU_HAS_NEON;
#else
    if (SDL_HasMMX())
        TVPCPUFeatures |= TVP_CPU_HAS_MMX;
    if (SDL_HasSSE())
        TVPCPUFeatures |= TVP_CPU_HAS_SSE;
    if (SDL_HasSSE2())
        TVPCPUFeatures |= TVP_CPU_HAS_SSE2;
    if (SDL_HasSSE3())
        TVPCPUFeatures |= TVP_CPU_HAS_SSE3;
    if (SDL_HasSSE41())
        TVPCPUFeatures |= TVP_CPU_HAS_SSE41;
    if (SDL_HasSSE42())
        TVPCPUFeatures |= TVP_CPU_HAS_SSE42;
    if (SDL_HasAVX())
        TVPCPUFeatures |= TVP_CPU_HAS_AVX;
    if (SDL_HasAVX2())
        TVPCPUFeatures |= TVP_CPU_HAS_AVX2;
    if (SDL_HasNEON())
        TVPCPUFeatures |= TVP_CPU_HAS_NEON;
#endif

    TVPCPUFeatures |= TVP_CPU_HAS_FPU;
    TVPCPUFeatures |= TVP_CPU_HAS_CMOV;
    TVPCPUFeatures |= TVP_CPU_HAS_TSC;

    TVPCPUType = (TVPCPUType & ~TVP_CPU_FEATURE_MASK) | (TVPCPUFeatures & TVP_CPU_FEATURE_MASK);

    ttstr log(TJS_N("CPU Vendor: "));
    log += ttstr(TVPCPUVendor);
    if (TVPCPUName[0])
        log += TJS_N(" / ") + ttstr(TVPCPUName);
    TVPAddImportantLog(log);
#ifdef _KRKRSDL3_OHOS
    log = TJS_N("CPU cores: ") + ttstr(sysconf(_SC_NPROCESSORS_ONLN));
    TVPAddImportantLog(log);
    struct sysinfo si;
    sysinfo(&si);
    log = TJS_N("CPU RAM: ") + ttstr((tjs_int64)(si.totalram / 1024 / 1024 / 1024)) + TJS_N("GB");
#else
    log = TJS_N("CPU cores: ") + ttstr(
#ifdef _KRKRSDL3_USE_SDL3
        SDL_GetNumLogicalCPUCores()
#else
        SDL_GetCPUCount()
#endif
    );
    TVPAddImportantLog(log);
    log = TJS_N("CPU RAM: ") + ttstr(SDL_GetSystemRAM() / 1024) + TJS_N("GB");
#endif
    TVPAddImportantLog(log);

    log = TJS_N("CPU features:");
#define LOG_FLAG(f, n) \
    if (TVPCPUFeatures & f) \
    log += TJS_N(" ") TJS_N(n)
    LOG_FLAG(TVP_CPU_HAS_MMX, "MMX");
    LOG_FLAG(TVP_CPU_HAS_SSE, "SSE");
    LOG_FLAG(TVP_CPU_HAS_SSE2, "SSE2");
    LOG_FLAG(TVP_CPU_HAS_SSE3, "SSE3");
    LOG_FLAG(TVP_CPU_HAS_SSE41, "SSE4.1");
    LOG_FLAG(TVP_CPU_HAS_SSE42, "SSE4.2");
    LOG_FLAG(TVP_CPU_HAS_AVX, "AVX");
    LOG_FLAG(TVP_CPU_HAS_AVX2, "AVX2");
    LOG_FLAG(TVP_CPU_HAS_NEON, "NEON");
    LOG_FLAG(TVP_CPU_HAS_FPU, "FPU");
    LOG_FLAG(TVP_CPU_HAS_CMOV, "CMOV");
    LOG_FLAG(TVP_CPU_HAS_TSC, "TSC");
    TVPAddImportantLog(log);
}
tjs_uint32 TVPGetCPUType()
{
    TVPDetectCPU();
    return TVPCPUType;
}

tjs_int TVPGetProcessorNum(void)
{
    static tjs_int processor_num = 0;
    if (!processor_num)
    {
#ifdef _KRKRSDL3_OHOS
        processor_num = (tjs_int)sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_KRKRSDL3_USE_SDL3)
        processor_num = SDL_GetNumLogicalCPUCores();
#else
        processor_num = SDL_GetCPUCount();
#endif
        tjs_char tmp[34];
        TVPAddLog(ttstr(TJS_N("Detected CPU core(s): ")) + TJS_tTVInt_to_str(processor_num, tmp));
    }
    return processor_num;
}
//---------------------------------------------------------------------------
