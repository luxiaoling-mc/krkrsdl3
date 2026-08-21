//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

     See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// configuration
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "Platform.h"

#include <ctime>

/*
 * core/utils/cp932_uni.cpp
 * core/utils/uni_cp932.cpp
 * を一緒にリンクしてください。
 * CP932(ShiftJIS) と Unicode 変換に使用しています。
 * Win32 APIの同等の関数は互換性等の問題があることやマルチプラットフォームの足かせとなる
 * ため使用が中止されました。
 */

static int utf8_mbtowc(tjs_wchar* pwc, const unsigned char* s, int n)
{
    unsigned char c = s[0];

    if (c < 0x80)
    {
        *pwc = c;
        return 1;
    }
    else if (c < 0xc2)
    {
        return -1;
    }
    else if (c < 0xe0)
    {
        if (n < 2)
            return -1;
        if (!((s[1] ^ 0x80) < 0x40))
            return -1;
        *pwc = ((tjs_wchar)(c & 0x1f) << 6) | (tjs_wchar)(s[1] ^ 0x80);
        return 2;
    }
    else if (c < 0xf0)
    {
        if (n < 3)
            return -1;
        if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40 && (c >= 0xe1 || s[1] >= 0xa0)))
            return -1;
        *pwc = ((tjs_wchar)(c & 0x0f) << 12) | ((tjs_wchar)(s[1] ^ 0x80) << 6) |
               (tjs_wchar)(s[2] ^ 0x80);
        return 3;
    }
    else
        return -1;
}

static int utf8_wctomb(unsigned char* r, tjs_wchar wc, int n)
{
    int count;
    if (wc < 0x80)
        count = 1;
    else if (wc < 0x800)
        count = 2;
    else
        count = 3;
    if (n < count)
        return -2;
    switch (count)
    {
        case 3:
            r[2] = 0x80 | (wc & 0x3f);
            wc = wc >> 6;
            wc |= 0x800;
        case 2:
            r[1] = 0x80 | (wc & 0x3f);
            wc = wc >> 6;
            wc |= 0xc0;
        case 1:
            r[0] = (unsigned char)wc;
    }
    return count;
}

namespace TJS
{
//---------------------------------------------------------------------------
// debug support
//---------------------------------------------------------------------------
#if TJS_DEBUG_PROFILE_TIME
tjs_uint TJSGetTickCount()
{
    return GetTickCount();
}
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// some wchar_t support functions
//---------------------------------------------------------------------------
tjs_int TJS_atoi(const tjs_char* s)
{
    int r = 0;
    bool sign = false;
    while (*s && *s <= 0x20)
        s++; // skip spaces
    if (!*s)
        return 0;
    if (*s == TJS_N('-'))
    {
        sign = true;
        s++;
        while (*s && *s <= 0x20)
            s++; // skip spaces
        if (!*s)
            return 0;
    }

    while (*s >= TJS_N('0') && *s <= TJS_N('9'))
    {
        r *= 10;
        r += *s - TJS_N('0');
        s++;
    }
    if (sign)
        r = -r;
    return r;
}

tjs_int64 TJS_atoll(const tjs_char* s)
{
    tjs_int64 r = 0;
    bool sign = false;
    while (*s && *s <= 0x20)
        s++; // skip spaces
    if (!*s)
        return 0;
    if (*s == TJS_N('-'))
    {
        sign = true;
        s++;
        while (*s && *s <= 0x20)
            s++; // skip spaces
        if (!*s)
            return 0;
    }

    while (*s >= TJS_N('0') && *s <= TJS_N('9'))
    {
        r *= 10;
        r += *s - TJS_N('0');
        s++;
    }
    if (sign)
        r = -r;
    return r;
}

tjs_char* TJS_int_to_str(tjs_int value, tjs_char* string)
{
    tjs_char* ostring = string;

    if (value < 0)
        *(string++) = TJS_N('-'), value = -value;

    tjs_char buf[40];

    tjs_char* p = buf;

    do
    {
        *(p++) = (value % 10) + TJS_N('0');
        value /= 10;
    } while (value);

    p--;
    while (buf <= p)
        *(string++) = *(p--);
    *string = 0;

    return ostring;
}

tjs_char* TJS_tTVInt_to_str(tjs_int64 value, tjs_char* string)
{
    if (value == TJS_UI64_VAL(0x8000000000000000))
    {
        // this is a special number which we must avoid normal conversion
        TJS_strcpy(string, TJS_N("-9223372036854775808"));
        return string;
    }

    tjs_char* ostring = string;

    if (value < 0)
        *(string++) = TJS_N('-'), value = -value;

    tjs_char buf[40];

    tjs_char* p = buf;

    do
    {
        *(p++) = (value % 10) + TJS_N('0');
        value /= 10;
    } while (value);

    p--;
    while (buf <= p)
        *(string++) = *(p--);
    *string = 0;

    return ostring;
}

tjs_int TJS_strnicmp(const tjs_char* s1, const tjs_char* s2, size_t maxlen)
{
    while (maxlen--)
    {
        if (*s1 == TJS_N('\0'))
            return (*s2 == TJS_N('\0')) ? 0 : -1;
        if (*s2 == TJS_N('\0'))
            return (*s1 == TJS_N('\0')) ? 0 : 1;
        if (*s1 < *s2)
            return -1;
        if (*s1 > *s2)
            return 1;
        s1++;
        s2++;
    }

    return 0;
}

tjs_int TJS_stricmp(const tjs_char* s1, const tjs_char* s2)
{
    // we only support basic alphabets
    // fixme: complete alphabets support

    for (;;)
    {
        tjs_char c1 = *s1, c2 = *s2;
        if (c1 >= TJS_N('a') && c1 <= TJS_N('z'))
            c1 += TJS_N('Z') - TJS_N('z');
        if (c2 >= TJS_N('a') && c2 <= TJS_N('z'))
            c2 += TJS_N('Z') - TJS_N('z');
        if (c1 == TJS_N('\0'))
            return (c2 == TJS_N('\0')) ? 0 : -1;
        if (c2 == TJS_N('\0'))
            return (c1 == TJS_N('\0')) ? 0 : 1;
        if (c1 < c2)
            return -1;
        if (c1 > c2)
            return 1;
        s1++;
        s2++;
    }
}

void TJS_strcpy_maxlen(tjs_char* d, const tjs_char* s, size_t len)
{
    tjs_char ch;
    len++;
    while ((ch = *s) != 0 && --len)
        *(d++) = ch, s++;
    *d = 0;
}

void TJS_strcpy(tjs_char* d, const tjs_char* s)
{
    tjs_char ch;
    while ((ch = *s) != 0)
        *(d++) = ch, s++;
    *d = 0;
}

size_t TJS_strlen(const tjs_char* d)
{
    const tjs_char* p = d;
    while (*d)
        d++;
    return d - p;
}

tjs_int TJS_sprintf(tjs_char* s, const tjs_char* format, ...)
{
    tjs_int r;
    va_list param;
    va_start(param, format);
    r = TJS_vsnprintf(s, INT_MAX, format, param);
    va_end(param);
    return r;
}
//---------------------------------------------------------------------------

tjs_int TJS_timezone()
{
    std::time_t now = std::time(nullptr);
    std::tm local_tm, utc_tm;

#if defined(_KRKRSDL3_WINDOWS)
    // Windows
    localtime_s(&local_tm, &now);
    gmtime_s(&utc_tm, &now);
#elif defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_LINUX) || defined(_KRKRSDL3_EMSCRIPTEN) || defined(_KRKRSDL3_OHOS) || defined(_KRKRSDL3_IOS)
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);
#else
#error "Unsupported platform"
#endif

    // 转换为时间戳（秒）
    std::time_t local_seconds = std::mktime(&local_tm);
    std::time_t utc_seconds = std::mktime(&utc_tm);

    // 计算偏移（秒转分钟）
    int32_t offset_seconds = static_cast<int32_t>(difftime(local_seconds, utc_seconds));
    return offset_seconds / 60;
}

//---------------------------------------------------------------------------
// Debug_Logout function
//---------------------------------------------------------------------------
void TJS_debug_out(const tjs_char* format, ...)
{
    tjs_int r;
    va_list param;
    va_start(param, format);
    tjs_char buf[256];
    r = TJS_vsnprintf(buf, 256, format, param);
    va_end(param);
    TVPConsoleLog(buf);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#define TJS_MB_MAX_CHARLEN 2
//---------------------------------------------------------------------------
size_t TJS_mbstowcs(tjs_wchar* pwcs, const tjs_char* s, size_t n)
{
    if (!s)
        return -1;
    if (pwcs && n == 0)
        return 0;

    tjs_wchar wc;
    size_t count = 0;
    int cl;
    if (!pwcs)
    {
        n = strlen(s);
        while (*s)
        {
            cl = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
            if (cl <= 0)
                break;
            s += cl;
            n -= cl;
            ++count;
        }
    }
    else
    {
        tjs_wchar* pwcsend = pwcs + n;
        n = strlen(s);
        while (*s && pwcs < pwcsend)
        {
            cl = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
            if (cl <= 0)
                return -1;
            s += cl;
            n -= cl;
            *pwcs++ = wc;
            ++count;
        }
    }
    return count;
}

size_t TJS_wcstombs(tjs_char* s, const tjs_wchar* pwcs, size_t n)
{
    if (!pwcs)
        return -1;
    if (s && !n)
        return 0;

    int cl;
    if (!s)
    {
        unsigned char tmp[6];
        size_t count = 0;
        while (*pwcs)
        {
            cl = utf8_wctomb(tmp, *pwcs, 6);
            if (cl <= 0)
                return -1;
            pwcs++;
            count += cl;
        }
        return count;
    }
    else
    {
        tjs_char* d = s;
        while (*pwcs && n > 0)
        {
            cl = utf8_wctomb((unsigned char*)d, *pwcs, (int)n);
            if (cl <= 0)
                return -1;
            n -= cl;
            d += cl;
            pwcs++;
        }
        return d - s;
    }
}
// 使われていないようなので未確認注意
int TJS_mbtowc(tjs_wchar* pwc, const tjs_char* s, size_t n)
{
    if (!s || !n)
        return 0;

    if (*s == 0)
    {
        if (pwc)
            *pwc = 0;
        return 0;
    }
    tjs_wchar wc;
    int ret = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
    if (ret >= 0)
    {
        *pwc = wc;
    }
    return ret;
}
// 使われていないようなので未確認注意
int TJS_wctomb(tjs_char* s, tjs_wchar wc)
{
    if (!s)
        return 0;
    tjs_wchar tmp[2] = {wc, 0};
    return utf8_wctomb((unsigned char*)s, wc, 2);
}
//---------------------------------------------------------------------------
tjs_int64 TVPWideCharToUtf8(tjs_wchar in, char* out)
{
    // convert a wide character 'in' to utf-8 character 'out'
    if (in < (1 << 7))
    {
        if (out)
        {
            out[0] = (char)in;
        }
        return 1;
    }
    else if (in < (1 << 11))
    {
        if (out)
        {
            out[0] = (char)(0xc0 | (in >> 6));
            out[1] = (char)(0x80 | (in & 0x3f));
        }
        return 2;
    }
    else
    {
        if (out)
        {
            out[0] = (char)(0xe0 | (in >> 12));
            out[1] = (char)(0x80 | ((in >> 6) & 0x3f));
            out[2] = (char)(0x80 | (in & 0x3f));
        }
        return 3;
    }
}
//---------------------------------------------------------------------------
tjs_int64 TVPWideCharToUtf8String(const tjs_wchar* in, char* out, size_t max_len)
{
    if (max_len <= 0)
    {
        // convert input wide string to output utf-8 string
        int count = 0;
        while (*in)
        {
            tjs_int n;
            if (out)
            {
                n = TVPWideCharToUtf8(*in, out);
                out += n;
            }
            else
            {
                n = TVPWideCharToUtf8(*in, NULL);
                /*
                        in this situation, the compiler's inliner
                        will collapse all null check parts in
                        TVPWideCharToUtf8.
                */
            }
            if (n == -1)
                return -1; // invalid character found
            count += n;
            in++;
        }
        return count;
    }
    else
    {
        tjs_int64 total_bytes = 0;
        size_t in_pos = 0;

        while (in[in_pos] && (max_len == 0 || in_pos < max_len))
        {
            uint32_t codepoint = in[in_pos];
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
            {
                if (in[in_pos + 1] >= 0xDC00 && in[in_pos + 1] <= 0xDFFF)
                {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (in[in_pos + 1] - 0xDC00);
                    in_pos++;
                }
                else
                {
                    codepoint = 0xFFFD;
                }
            }
            else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
            {
                codepoint = 0xFFFD;
            }
            if (codepoint <= 0x7F)
            {
                if (out)
                {
                    out[total_bytes] = static_cast<char>(codepoint);
                }
                total_bytes += 1;
            }
            else if (codepoint <= 0x7FF)
            {
                if (out)
                {
                    out[total_bytes] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                    out[total_bytes + 1] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                total_bytes += 2;
            }
            else if (codepoint <= 0xFFFF)
            {
                if (out)
                {
                    out[total_bytes] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                    out[total_bytes + 1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out[total_bytes + 2] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                total_bytes += 3;
            }
            else if (codepoint <= 0x10FFFF)
            {
                if (out)
                {
                    out[total_bytes] = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
                    out[total_bytes + 1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    out[total_bytes + 2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out[total_bytes + 3] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                total_bytes += 4;
            }

            in_pos++;
        }
        return total_bytes;
    }
}
//---------------------------------------------------------------------------
static bool inline TVPUtf8ToWideChar(const char*& in, tjs_wchar* out)
{
    // convert a utf-8 charater from 'in' to wide charater 'out'
    const unsigned char*& p = (const unsigned char*&)in;
    if (p[0] < 0x80)
    {
        if (out)
            *out = (tjs_wchar)in[0];
        in++;
        return true;
    }
    else if (p[0] < 0xc2)
    {
        // invalid character
        return false;
    }
    else if (p[0] < 0xe0)
    {
        // two bytes (11bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
        in += 2;
        return true;
    }
    else if (p[0] < 0xf0)
    {
        // three bytes (16bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x1f) << 12) + ((p[1] & 0x3f) << 6) + (p[2] & 0x3f);
        in += 3;
        return true;
    }
    else if (p[0] < 0xf8)
    {
        // four bytes (21bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12) + ((p[2] & 0x3f) << 6) +
                   (p[3] & 0x3f);
        in += 4;
        return true;
    }
    else if (p[0] < 0xfc)
    {
        // five bytes (26bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if ((p[4] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x03) << 24) + ((p[1] & 0x3f) << 18) + ((p[2] & 0x3f) << 12) +
                   ((p[3] & 0x3f) << 6) + (p[4] & 0x3f);
        in += 5;
        return true;
    }
    else if (p[0] < 0xfe)
    {
        // six bytes (31bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if ((p[4] & 0xc0) != 0x80)
            return false;
        if ((p[5] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x01) << 30) + ((p[1] & 0x3f) << 24) + ((p[2] & 0x3f) << 18) +
                   ((p[3] & 0x3f) << 12) + ((p[4] & 0x3f) << 6) + (p[5] & 0x3f);
        in += 6;
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
bool TVP_utf8_to_utf16(const char* in, tjs_wchar* out)
{
    const unsigned char* p = (const unsigned char*)in;
    if (p[0] < 0x80)
    {
        if (out)
            *out = (tjs_wchar)in[0];
        return true;
    }
    else if (p[0] < 0xc2)
    {
        // invalid character
        return false;
    }
    else if (p[0] < 0xe0)
    {
        // two bytes (11bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
        return true;
    }
    else if (p[0] < 0xf0)
    {
        // three bytes (16bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x1f) << 12) + ((p[1] & 0x3f) << 6) + (p[2] & 0x3f);
        return true;
    }
    else if (p[0] < 0xf8)
    {
        // four bytes (21bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12) + ((p[2] & 0x3f) << 6) +
                   (p[3] & 0x3f);
        return true;
    }
    else if (p[0] < 0xfc)
    {
        // five bytes (26bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if ((p[4] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x03) << 24) + ((p[1] & 0x3f) << 18) + ((p[2] & 0x3f) << 12) +
                   ((p[3] & 0x3f) << 6) + (p[4] & 0x3f);
        return true;
    }
    else if (p[0] < 0xfe)
    {
        // six bytes (31bits)
        if ((p[1] & 0xc0) != 0x80)
            return false;
        if ((p[2] & 0xc0) != 0x80)
            return false;
        if ((p[3] & 0xc0) != 0x80)
            return false;
        if ((p[4] & 0xc0) != 0x80)
            return false;
        if ((p[5] & 0xc0) != 0x80)
            return false;
        if (out)
            *out = ((p[0] & 0x01) << 30) + ((p[1] & 0x3f) << 24) + ((p[2] & 0x3f) << 18) +
                   ((p[3] & 0x3f) << 12) + ((p[4] & 0x3f) << 6) + (p[5] & 0x3f);
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
tjs_int64 TVPUtf8ToWideCharString(const char* in, tjs_wchar* out)
{
    // convert input utf-8 string to output wide string
    int count = 0;
    while (*in)
    {
        tjs_wchar c;
        if (out)
        {
            if (!TVPUtf8ToWideChar(in, &c))
                return -1; // invalid character found
            *out++ = c;
        }
        else
        {
            if (!TVPUtf8ToWideChar(in, NULL))
                return -1; // invalid character found
        }
        count++;
    }
    return count;
}
//---------------------------------------------------------------------------
tjs_int64 TVPUtf8ToWideCharString(const char* in, tjs_uint length, tjs_wchar* out)
{
    // convert input utf-8 string to output wide string
    int count = 0;
    const char* end = in + length;
    while (*in && in < end)
    {
        if (in + 6 > end)
        {
            // fetch utf-8 character length
            const unsigned char ch = *(const unsigned char*)in;

            if (ch >= 0x80)
            {
                tjs_uint len = 0;

                if (ch < 0xc2)
                    return -1;
                else if (ch < 0xe0)
                    len = 2;
                else if (ch < 0xf0)
                    len = 3;
                else if (ch < 0xf8)
                    len = 4;
                else if (ch < 0xfc)
                    len = 5;
                else if (ch < 0xfe)
                    len = 6;
                else
                    return -1;

                if (in + len > end)
                    return -1;
            }
        }

        tjs_wchar c;
        if (out)
        {
            if (!TVPUtf8ToWideChar(in, &c))
                return -1; // invalid character found
            *out++ = c;
        }
        else
        {
            if (!TVPUtf8ToWideChar(in, NULL))
                return -1; // invalid character found
        }
        count++;
    }
    return count;
}
//---------------------------------------------------------------------------
std::vector<uint32_t> decodeUTF8ToTTF(const char* utf8_str)
{
    std::vector<uint32_t> codepoints;
    const unsigned char* str = reinterpret_cast<const unsigned char*>(utf8_str);
    while (*str)
    {
        uint32_t codepoint = 0;
        if ((*str & 0x80) == 0)
        {
            codepoint = *str;
            str += 1;
        }
        else if ((*str & 0xE0) == 0xC0)
        {
            codepoint = (*str & 0x1F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else if ((*str & 0xF0) == 0xE0)
        {
            codepoint = (*str & 0x0F) << 12;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else if ((*str & 0xF8) == 0xF0)
        {
            codepoint = (*str & 0x07) << 18;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 12;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else
        {
            break;
        }
        codepoints.push_back(codepoint);
    }
    return codepoints;
}
//---------------------------------------------------------------------------
std::vector<tjs_wchar> decodeUTF8ToTTFe(const char* utf8_str)
{
    std::vector<tjs_wchar> codepoints;
    const unsigned char* str = reinterpret_cast<const unsigned char*>(utf8_str);
    while (*str)
    {
        tjs_wchar codepoint = 0;
        if ((*str & 0x80) == 0)
        {
            codepoint = *str;
            str += 1;
        }
        else if ((*str & 0xE0) == 0xC0)
        {
            codepoint = (*str & 0x1F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else if ((*str & 0xF0) == 0xE0)
        {
            codepoint = (*str & 0x0F) << 12;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else if ((*str & 0xF8) == 0xF0)
        {
            codepoint = (*str & 0x07) << 18;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 12;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F) << 6;
            str += 1;
            if ((*str & 0xC0) != 0x80)
                break;
            codepoint |= (*str & 0x3F);
            str += 1;
        }
        else
        {
            break;
        }
        codepoints.push_back(codepoint);
    }
    return codepoints;
}
//---------------------------------------------------------------------------
tjs_int64 utf8_char_len(const char* str)
{
    if (!str || !*str)
        return 0;
    unsigned char c = *reinterpret_cast<const unsigned char*>(str);
    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 0;
}
//---------------------------------------------------------------------------
tjs_int64 utf8_chars_len(const char* str)
{
    tjs_int count = 0;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++)
        count += ((*p & 0xC0) != 0x80);
    return count;
}
//---------------------------------------------------------------------------
tjs_int64 utf8_indexOf(const char* data, const char* chs, int startpos)
{
    if (!data || !chs || startpos < 0)
        return -1;
    if (!*chs)
        return startpos;
    const unsigned char* data_ptr = reinterpret_cast<const unsigned char*>(data);
    const unsigned char* search_ptr = reinterpret_cast<const unsigned char*>(chs);
    int search_bytes = std::strlen(chs);
    int search_chars = utf8_chars_len(chs);
    if (search_bytes == 0)
        return startpos;

    int data_char_index = 0;
    const unsigned char* data_start = data_ptr;
    while (*data_ptr)
    {
        if (data_char_index < startpos)
        {
            int len = utf8_char_len(reinterpret_cast<const char*>(data_ptr));
            if (len == 0)
                len = 1;
            data_ptr += len;
            data_char_index++;
            continue;
        }
        if (strlen(reinterpret_cast<const char*>(data_ptr)) < search_bytes)
        {
            return -1;
        }
        if (std::memcmp(data_ptr, search_ptr, search_bytes) == 0)
        {
            return data_char_index;
        }
        int len = utf8_char_len(reinterpret_cast<const char*>(data_ptr));
        if (len == 0)
            len = 1;
        data_ptr += len;
        data_char_index++;
    }
    return -1;
}
//---------------------------------------------------------------------------
const char* utf8_substr(const char* data, int start, int len, int& retLen)
{
    if (!data || start < 0)
    {
        retLen = 0;
        return NULL;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    int char_index = 0;
    const unsigned char* start_ptr = NULL;
    while (*p)
    {
        if (char_index == start)
        {
            start_ptr = p;
            break;
        }

        int clen = utf8_char_len(reinterpret_cast<const char*>(p));
        if (clen == 0)
            clen = 1;
        p += clen;
        char_index++;
    }
    if (!start_ptr)
    {
        retLen = 0;
        return NULL;
    }

    int bytes = 0;
    if (len < 0)
    {
        bytes = std::strlen(reinterpret_cast<const char*>(p));
    }
    else
    {
        p = start_ptr;
        int taken = 0;
        while (*p && taken < len)
        {
            int clen = utf8_char_len(reinterpret_cast<const char*>(p));
            if (clen == 0)
                clen = 1;
            bytes += clen;
            p += clen;
            taken++;
        }
    }
    retLen = bytes;
    return reinterpret_cast<const char*>(start_ptr);
}
//---------------------------------------------------------------------------
void utf8_reverse(const char* data, char* outputdata)
{
    if (!data || !outputdata)
        return;
    std::vector<const char*> char_pointers;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    while (*p)
    {
        char_pointers.push_back(reinterpret_cast<const char*>(p));
        int len = utf8_char_len(reinterpret_cast<const char*>(p));
        if (len == 0)
            len = 1;
        p += len;
    }

    // 反向复制字符
    char* t = outputdata;
    for (int i = char_pointers.size() - 1; i >= 0; i--)
    {
        const char* src = char_pointers[i];
        int len = utf8_char_len(src);
        if (len == 0)
            len = 1;
        for (int j = 0; j < len; j++)
        {
            *t++ = src[j];
        }
    }
    *t = '\0';
}
//---------------------------------------------------------------------------
const char* utf8_char_get(const char* str, int idx)
{
    if (!str || idx < 0)
        return NULL;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str);
    int current_idx = 0;
    while (*p)
    {
        if (current_idx == idx)
        {
            return reinterpret_cast<const char*>(p);
        }
        if ((*p & 0x80) == 0)
        {
            p += 1;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            p += 4;
        }
        else
        {
            p++;
        }
        current_idx++;
    }
    return NULL;
}
//---------------------------------------------------------------------------
bool TJS_iswspace(const char* chs)
{
    unsigned char c = *reinterpret_cast<const unsigned char*>(chs);
    if ((c & 0x80) == 0)
    {
        bool is_ascii_space =
            (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
        if (is_ascii_space)
        {
            return true;
        }
        return false;
    }
    return false;
}
//---------------------------------------------------------------------------
bool TJS_iswdigit(const char* chs)
{
    unsigned char c = *reinterpret_cast<const unsigned char*>(chs);
    if ((c & 0x80) == 0)
    {
        if (c >= '0' && c <= '9')
        {
            return true;
        }
        return false;
    }
    return false;
}
//---------------------------------------------------------------------------
tjs_int TJS_iswalpha(const char* chs)
{
    unsigned char c = *reinterpret_cast<const unsigned char*>(chs);

    if ((c & 0x80) == 0)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        {
            return 1;
        }
        return 0;
    }
    int len = 0;
    if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    return len;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// native debugger break point
//---------------------------------------------------------------------------
void TJSNativeDebuggerBreak()
{
    // This function is to be called mostly when the "debugger" TJS statement is
    // executed.
    // Step you debbuger back to the the caller, and continue debugging.
    // Do not use "debugger" statement unless you run the program under the native
    // debugger, or the program may cause an unhandled debugger breakpoint
    // exception.

#if defined(__WIN32__)
#if defined(_M_IX86)
#ifdef __BORLANDC__
    __emit__(0xcc); // int 3 (Raise debugger breakpoint exception)
#else
    _asm _emit 0xcc; // int 3 (Raise debugger breakpoint exception)
#endif
#else
    __debugbreak();
#endif
#endif
}

//---------------------------------------------------------------------------
// FPU control
//---------------------------------------------------------------------------
#if defined(__WIN32__) && !defined(__GNUC__)
static unsigned int TJSDefaultFPUCW = 0;
static unsigned int TJSNewFPUCW = 0;
static unsigned int TJSDefaultMMCW = 0;
static bool TJSFPUInit = false;
#endif
// FPU例外をマスク
void TJSSetFPUE()
{
#if defined(__WIN32__) && !defined(__GNUC__)
    if (!TJSFPUInit)
    {
        TJSFPUInit = true;
#if defined(_M_X64)
        TJSDefaultMMCW = _MM_GET_EXCEPTION_MASK();
#else
        TJSDefaultFPUCW = _control87(0, 0);

#ifdef _MSC_VER
        TJSNewFPUCW = _control87(MCW_EM, MCW_EM);
#else
        _default87 = TJSNewFPUCW = _control87(MCW_EM, MCW_EM);
#endif // _MSC_VER
#endif // _M_X64
    }

#if defined(_M_X64)
    _MM_SET_EXCEPTION_MASK(_MM_MASK_INVALID | _MM_MASK_DIV_ZERO | _MM_MASK_DENORM |
                           _MM_MASK_OVERFLOW | _MM_MASK_UNDERFLOW | _MM_MASK_INEXACT);
#else
    //	_fpreset();
    _control87(TJSNewFPUCW, 0xffff);
#endif
#endif // defined(__WIN32__) && !defined(__GNUC__)
}
// 例外マスクを解除し元に戻す
void TJSRestoreFPUE()
{
#if defined(__WIN32__) && !defined(__GNUC__)
    if (!TJSFPUInit)
        return;
#if defined(_M_X64)
    _MM_SET_EXCEPTION_MASK(TJSDefaultMMCW);
#else
    _control87(TJSDefaultFPUCW, 0xffff);
#endif
#endif
}
//---------------------------------------------------------------------------

int TJS_strcmp(const tjs_char* src, const tjs_char* dst)
{
    if (src == NULL || dst == NULL)
    {
        if (src == NULL && dst == NULL)
            return 0;
        if (src == NULL)
            return -1;
        return 1;
    }
    return strcmp(src, dst);
}

int TJS_strncmp(const tjs_char* first, const tjs_char* last, size_t count)
{
    if (count == 0)
    {
        return 0;
    }
    if (first == NULL || last == NULL)
    {
        if (first == NULL && last == NULL)
        {
            return 0;
        }
        if (first == NULL)
        {
            return -1;
        }
        return 1;
    }
    return strncmp(first, last, count);
}

int TJS_strcasecmp(const tjs_char* s1, const tjs_char* s2)
{
    if (s1 == s2) return 0;
    if (s1 == NULL) return -1;
    if (s2 == NULL) return 1;
    
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (c1 > c2) ? 1 : -1;
        s1++; s2++;
    }
    if (*s1) return 1;
    if (*s2) return -1;
    return 0;
}

int TJS_strncasecmp(const tjs_char* s1, const tjs_char* s2, size_t n)
{
    if (s1 == s2 || n == 0) return 0;
    if (s1 == NULL) return -1;
    if (s2 == NULL) return 1;
    
    while (n-- > 0 && *s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (c1 > c2) ? 1 : -1;
        s1++; s2++;
    }
    
    if (n == (size_t)-1) return 0;
    if (*s1) return 1;
    if (*s2) return -1;
    return 0;
}

tjs_char* TJS_strncpy(tjs_char* dest, const tjs_char* source, size_t count)
{
    tjs_char* start = dest;

    while (count && (*dest++ = *source++)) /* copy string */
        count--;

    if (count) /* pad out with zeroes */
        while (--count)
            *dest++ = '\0';

    return (start);
}

tjs_char* TJS_strcat(tjs_char* dst, const tjs_char* src)
{
    tjs_char* cp = dst;

    while (*cp)
        cp++; /* find end of dst */

    while ((*cp++ = *src++))
        ; /* Copy src to end of dst */

    return (dst); /* return dst */
}

tjs_char* TJS_strstr(const tjs_char* wcs1, const tjs_char* wcs2)
{
    tjs_char* cp = (tjs_char*)wcs1;
    tjs_char *s1, *s2;

    if (!*wcs2)
        return (tjs_char*)wcs1;

    while (*cp)
    {
        s1 = cp;
        s2 = (tjs_char*)wcs2;

        while (*s1 && *s2 && !(*s1 - *s2))
            s1++, s2++;

        if (!*s2)
            return (cp);

        cp++;
    }

    return (NULL);
}

tjs_char* TJS_strchr(const tjs_char* string, tjs_char ch)
{
    while (*string && *string != (tjs_char)ch)
        string++;

    if (*string == (tjs_char)ch)
        return ((tjs_char*)string);
    return (NULL);
}

void* TJS_malloc(size_t len)
{
    char* ret = (char*)malloc(len + sizeof(size_t));
    if (!ret)
        return nullptr;
    *(size_t*)ret = len; // embed size
    return ret + sizeof(size_t);
}

void* TJS_realloc(void* buf, size_t len)
{
    if (!buf)
        return TJS_malloc(len);
    // compare embeded size
    size_t* ptr = (size_t*)((char*)buf - sizeof(size_t));
    if (*ptr >= len)
        return buf; // still adequate
    char* ret = (char*)TJS_malloc(len);
    if (!ret)
        return nullptr;
    memcpy(ret, ptr + 1, *ptr);
    TJS_free(buf);
    return ret;
}

void TJS_free(void* buf)
{
    free((char*)buf - sizeof(size_t));
}

tjs_char* TJS_strrchr(const tjs_char* s, int c)
{
    tjs_char* ret = 0;
    do
    {
        if (*s == (char)c)
            ret = (tjs_char*)s;
    } while (*s++);
    return ret;
}

double TJS_strtod(const tjs_char* string, tjs_char** endPtr)
{
    int sign, expSign = false;
    double fraction, dblExp;
    const double* d;
    const tjs_char* p;
    int c;
    int exp = 0;          /* Exponent read from "EX" field. */
    int fracExp = 0;      /* Exponent that derives from the fractional
                           * part.  Under normal circumstatnces, it is
                           * the negative of the number of digits in F.
                           * However, if I is very long, the last digits
                           * of I get dropped (otherwise a long I with a
                           * large negative exponent could cause an
                           * unnecessary overflow on I alone).  In this
                           * case, fracExp is incremented one for each
                           * dropped digit. */
    int mantSize;         /* Number of digits in mantissa. */
    int decPt;            /* Number of mantissa digits BEFORE decimal
                           * point. */
    const tjs_char* pExp; /* Temporarily holds location of exponent
                           * in string. */
    static const int maxExponent = 511;
    static const double powersOf10[] = {      /* Table giving binary powers of 10.  Entry */
                                        10.,  /* is 10^2^i.  Used to convert decimal */
                                        100., /* exponents into floating-point numbers. */
                                        1.0e4, 1.0e8, 1.0e16, 1.0e32, 1.0e64, 1.0e128, 1.0e256};
    /*
     * Strip off leading blanks and check for a sign.
     */

    p = string;
    while (isspace((*p)))
    {
        p += 1;
    }
    if (*p == '-')
    {
        sign = true;
        p += 1;
    }
    else
    {
        if (*p == '+')
        {
            p += 1;
        }
        sign = false;
    }

    /*
     * Count the number of digits in the mantissa (including the decimal
     * point), and also locate the decimal point.
     */

    decPt = -1;
    for (mantSize = 0;; mantSize += 1)
    {
        c = *p;
        if (!isdigit(c))
        {
            if ((c != '.') || (decPt >= 0))
            {
                break;
            }
            decPt = mantSize;
        }
        p += 1;
    }

    /*
     * Now suck up the digits in the mantissa.  Use two integers to
     * collect 9 digits each (this is faster than using floating-point).
     * If the mantissa has more than 18 digits, ignore the extras, since
     * they can't affect the value anyway.
     */

    pExp = p;
    p -= mantSize;
    if (decPt < 0)
    {
        decPt = mantSize;
    }
    else
    {
        mantSize -= 1; /* One of the digits was the point. */
    }
    if (mantSize > 48)
    {
        fracExp = decPt - 48;
        mantSize = 48;
    }
    else
    {
        fracExp = decPt - mantSize;
    }
    if (mantSize == 0)
    {
        fraction = 0.0;
        p = string;
        goto done;
    }
    else
    {
        int frac1, frac2;
        frac1 = 0;
        for (; mantSize > 9; mantSize -= 1)
        {
            c = *p;
            p += 1;
            if (c == '.')
            {
                c = *p;
                p += 1;
            }
            frac1 = 10 * frac1 + (c - '0');
        }
        frac2 = 0;
        for (; mantSize > 0; mantSize -= 1)
        {
            c = *p;
            p += 1;
            if (c == '.')
            {
                c = *p;
                p += 1;
            }
            frac2 = 10 * frac2 + (c - '0');
        }
        fraction = (1.0e9 * frac1) + frac2;
    }

    /*
     * Skim off the exponent.
     */

    p = pExp;
    if ((*p == 'E') || (*p == 'e'))
    {
        p += 1;
        if (*p == '-')
        {
            expSign = true;
            p += 1;
        }
        else
        {
            if (*p == '+')
            {
                p += 1;
            }
            expSign = false;
        }
        if (!isdigit((*p)))
        {
            p = pExp;
            goto done;
        }
        while (isdigit((*p)))
        {
            exp = exp * 10 + (*p - '0');
            p += 1;
        }
    }
    if (expSign)
    {
        exp = fracExp - exp;
    }
    else
    {
        exp = fracExp + exp;
    }

    /*
     * Generate a floating-point number that represents the exponent.
     * Do this by processing the exponent one bit at a time to combine
     * many powers of 2 of 10. Then combine the exponent with the
     * fraction.
     */

    if (exp < 0)
    {
        expSign = true;
        exp = -exp;
    }
    else
    {
        expSign = false;
    }
    if (exp > maxExponent)
    {
        exp = maxExponent;
        errno = ERANGE;
    }
    dblExp = 1.0;
    for (d = powersOf10; exp != 0; exp >>= 1, d += 1)
    {
        if (exp & 01)
        {
            dblExp *= *d;
        }
    }
    if (expSign)
    {
        fraction /= dblExp;
    }
    else
    {
        fraction *= dblExp;
    }

done:
    if (endPtr != NULL)
    {
        *endPtr = (tjs_char*)p;
    }

    if (sign)
    {
        return -fraction;
    }
    return fraction;
}

size_t TJS_strftime(tjs_char* wstring, size_t maxsize, const tjs_char* wformat, const tm* timeptr)
{
    return strftime(wstring, maxsize, wformat, timeptr);
}

int TJS_vsnprintf(tjs_char* string, size_t count, const tjs_char* format, va_list ap)
{
    try
    {
        va_list ap_copy;
        va_copy(ap_copy, ap);
        int result = std::vsnprintf(string, count, format, ap_copy);
        va_end(ap_copy);
        if (result < 0 || static_cast<size_t>(result) >= count)
        {
            return 0;
        }
        return result;
    }
    catch (...)
    {
        return 0;
    }
}

tjs_int TJS_snprintf(tjs_char* s, size_t count, const tjs_char* format, ...)
{
    tjs_int r;
    va_list param;
    va_start(param, format);
    r = TJS_vsnprintf(s, count, format, param);
    va_end(param);
    return r;
}
} // namespace TJS
