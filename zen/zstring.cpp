// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#include "zstring.h"
    #include <glib.h>
    #include "sys_error.h"

using namespace zen;


Zstring getUnicodeNormalFormNonAscii(const Zstring& str)
{
    //Example: const char* decomposed  = "\x6f\xcc\x81";
    //         const char* precomposed = "\xc3\xb3";
    assert(!isAsciiString(str));
    assert(str.find(Zchar('\0')) == Zstring::npos); //don't expect embedded nulls!

    try
    {
        gchar* outStr = ::g_utf8_normalize(str.c_str(), str.length(), G_NORMALIZE_DEFAULT_COMPOSE);
        if (!outStr)
            throw SysError(formatSystemError("g_utf8_normalize", L"", L"Conversion failed."));
        ZEN_ON_SCOPE_EXIT(::g_free(outStr));
        return outStr;

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error normalizing string:" +
                                 '\n' + utfTo<std::string>(str)  + "\n\n" + utfTo<std::string>(e.toString()));
    }
}


Zstring getUnicodeNormalForm(const Zstring& str)
{
    //fast pre-check:
    if (isAsciiString(str)) //perf: in the range of 3.5ns
        return str;
    static_assert(std::is_same_v<decltype(str), const Zbase<Zchar>&>, "god bless our ref-counting! => save output string memory consumption!");

    return getUnicodeNormalFormNonAscii(str);
}


Zstring getUpperCaseNonAscii(const Zstring& str)
{
    Zstring strNorm = getUnicodeNormalFormNonAscii(str);
    try
    {
        static_assert(sizeof(impl::CodePoint) == sizeof(gunichar));
        Zstring output;
        output.reserve(strNorm.size());

        UtfDecoder<char> decoder(strNorm.c_str(), strNorm.size());
        while (const std::optional<impl::CodePoint> cp = decoder.getNext())
            impl::codePointToUtf<char>(::g_unichar_toupper(*cp), [&](char c) { output += c; }); //don't use std::towupper: *incomplete* and locale-dependent!

        return output;

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error converting string to upper case:" +
                                 '\n' + utfTo<std::string>(str)  + "\n\n" + utfTo<std::string>(e.toString()));
    }
}


Zstring getUpperCase(const Zstring& str)
{
    if (isAsciiString(str)) //fast path: in the range of 3.5ns
    {
        Zstring output = str;
        for (Zchar& c : output)  //identical to LCMapStringEx(), g_unichar_toupper(), CFStringUppercase() [verified!]
            c = asciiToUpper(c); //
        return output;
    }
    //else: slow path --------------------------------------

    return getUpperCaseNonAscii(str);
}


namespace
{
std::weak_ordering compareNoCaseUtf8(const char* lhs, size_t lhsLen, const char* rhs, size_t rhsLen)
{
    //- strncasecmp implements ASCII CI-comparsion only! => signature is broken for UTF8-input; toupper() similarly doesn't support Unicode
    //- wcsncasecmp: https://opensource.apple.com/source/Libc/Libc-763.12/string/wcsncasecmp-fbsd.c
    // => re-implement comparison based on g_unichar_tolower() to avoid memory allocations

    UtfDecoder<char> decL(lhs, lhsLen);
    UtfDecoder<char> decR(rhs, rhsLen);
    for (;;)
    {
        const std::optional<impl::CodePoint> cpL = decL.getNext();
        const std::optional<impl::CodePoint> cpR = decR.getNext();
        if (!cpL || !cpR)
            return !cpR <=> !cpL;

        static_assert(sizeof(gunichar) == sizeof(impl::CodePoint));

        //ordering: "to lower" converts to higher code points than "to upper"
        const gunichar charL = ::g_unichar_toupper(*cpL); //note: tolower can be ambiguous, so don't use:
        const gunichar charR = ::g_unichar_toupper(*cpR); //e.g. "Σ" (upper case) can be lower-case "ς" in the end of the word or "σ" in the middle.
        if (charL != charR)
            return makeUnsigned(charL) <=> makeUnsigned(charR); //unsigned char-comparison is the convention!
    }
}
}


std::weak_ordering compareNatural(const Zstring& lhs, const Zstring& rhs)
{
    /* Unicode normal forms:
          Windows: CompareString() already ignores NFD/NFC differences: nice...
          Linux:  g_unichar_toupper() can't ignore differences
          macOS:  CFStringCompare() considers differences */
    try
    {
        const Zstring& lhsNorm = getUnicodeNormalForm(lhs);
        const Zstring& rhsNorm = getUnicodeNormalForm(rhs);

        const char* strL = lhsNorm.c_str();
        const char* strR = rhsNorm.c_str();

        const char* const strEndL = strL + lhsNorm.size();
        const char* const strEndR = strR + rhsNorm.size();
        /*  - compare strings after conceptually creating blocks of whitespace/numbers/text
            - implement strict weak ordering!
            - don't follow broken "strnatcasecmp": https://github.com/php/php-src/blob/master/ext/standard/strnatcmp.c
                    1. incorrect non-ASCII CI-comparison
                    2. incorrect bounds checks
                    3. incorrect trimming of *all* whitespace
                    4. arbitrary handling of leading 0 only at string begin
                    5. incorrect handling of whitespace following a number
                    6. code is a mess                                          */
        for (;;)
        {
            if (strL == strEndL || strR == strEndR)
                return (strL != strEndL) <=> (strR != strEndR); //"nothing" before "something"
            //note: "something" never would have been condensed to "nothing" further below => can finish evaluation here

            const bool wsL = isWhiteSpace(*strL);
            const bool wsR = isWhiteSpace(*strR);
            if (wsL != wsR)
                return !wsL <=> !wsR; //whitespace before non-ws!
            if (wsL)
            {
                ++strL, ++strR;
                while (strL != strEndL && isWhiteSpace(*strL)) ++strL;
                while (strR != strEndR && isWhiteSpace(*strR)) ++strR;
                continue;
            }

            const bool digitL = isDigit(*strL);
            const bool digitR = isDigit(*strR);
            if (digitL != digitR)
                return !digitL <=> !digitR; //numbers before chars!
            if (digitL)
            {
                while (strL != strEndL && *strL == '0') ++strL;
                while (strR != strEndR && *strR == '0') ++strR;

                int rv = 0;
                for (;; ++strL, ++strR)
                {
                    const bool endL = strL == strEndL || !isDigit(*strL);
                    const bool endR = strR == strEndR || !isDigit(*strR);
                    if (endL != endR)
                        return !endL <=> !endR; //more digits means bigger number
                    if (endL)
                        break; //same number of digits

                    if (rv == 0 && *strL != *strR)
                        rv = *strL - *strR; //found first digit difference comparing from left
                }
                if (rv != 0)
                    return rv <=> 0;
                continue;
            }

            //compare full junks of text: consider unicode encoding!
            const char* textBeginL = strL++;
            const char* textBeginR = strR++; //current char is neither white space nor digit at this point!
            while (strL != strEndL && !isWhiteSpace(*strL) && !isDigit(*strL)) ++strL;
            while (strR != strEndR && !isWhiteSpace(*strR) && !isDigit(*strR)) ++strR;

            if (const std::weak_ordering cmp = compareNoCaseUtf8(textBeginL, strL - textBeginL, textBeginR, strR - textBeginR);
                cmp != std::weak_ordering::equivalent)
                return cmp;
        }

    }
    catch (const SysError& e)
    {
        throw std::runtime_error(std::string(__FILE__) + '[' + numberTo<std::string>(__LINE__) + "] Error comparing strings:" + '\n' +
                                 utfTo<std::string>(lhs) + '\n' + utfTo<std::string>(rhs) + "\n\n" + utfTo<std::string>(e.toString()));
    }
}


std::weak_ordering compareNoCase(const Zstring& lhs, const Zstring& rhs)
{
    //fast path: no need for extra memory allocations => ~ 6x speedup
    const size_t minSize = std::min(lhs.size(), rhs.size());

    size_t i = 0;
    for (; i < minSize; ++i)
    {
        const Zchar l = lhs[i];
        const Zchar r = rhs[i];
        if (!isAsciiChar(l) || !isAsciiChar(r))
            goto slowPath; //=> let's NOT make assumptions how getUpperCase() compares "ASCII <=> non-ASCII"

        const Zchar lUp = asciiToUpper(l); //
        const Zchar rUp = asciiToUpper(r); //no surprises: emulate getUpperCase() [verified!]
        if (lUp != rUp)                    //
            return lUp <=> rUp;            //
    }
    return lhs.size() <=> rhs.size();
slowPath: //--------------------------------------

    return compareNoCaseUtf8(lhs.c_str() + i, lhs.size() - i, 
                             rhs.c_str() + i, rhs.size() - i);
}
