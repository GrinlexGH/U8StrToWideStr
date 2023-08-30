#pragma once

#include <cstdint>
#include <stdexcept>

#include "charconverters.hpp"

namespace CharConverters
{
    std::u8string WideStrToUTF8(const std::wstring_view in) {
        std::u8string out;
        // Preallocate memory for the "out" string to reduce the number of reallocations
        out.reserve(in.size() * 4);
        uint32_t codePoint = 0;
        for (size_t i = 0; i < in.size(); ++i) {
            wchar_t wchar = in[i];
            /*
            * +--------------+--------------+----------+----------+----------+----------+
            * | Code point   | Code point   | Byte 1   | Byte 2   | Byte 3   | Byte 4   |
            * | (First)      | (Last)       |          |          |          |          |
            * +--------------+--------------+----------+----------+----------+----------+
            * | U+0000       | U+007F       | 0xxxxxxx |          |          |          |
            * | U+0080       | U+07FF       | 110xxxxx | 10xxxxxx |          |          |
            * | U+0800       | U+FFFF       | 1110xxxx | 10xxxxxx | 10xxxxxx |          |
            * | U+10000      | U+10FFFF     | 11110xxx | 10xxxxxx | 10xxxxxx | 10xxxxxx |
            * +--------------+--------------+----------+----------+----------+----------+
            */
            // Single-byte character
            if (wchar <= 0x7F) {
                out.push_back(static_cast<char8_t>(wchar));
            }
            // Two-byte character
            else if (wchar <= 0x07FF) {
                /*
                * For example, let's take the character 0000001110100110 (0x03A6) and
                * convert it to 11001110 10100110 (0xCE 0xA6)
                * First byte:
                * 1. Shift right by 6: 00001110
                * 2. Add 11000000: 11001110 -- this gives us the first byte
                * Second byte:
                * 1. Apply a mask 00111111 (0x3F) to 10100110 (first 8 bits of 0x03A6): 00100110
                * 2. Add 10000000 (0x80): 10100110
                */
                out.push_back(static_cast<char8_t>((wchar >> 6) | 0xC0));
                out.push_back(static_cast<char8_t>((wchar & 0x3F) | 0x80));
            }
            // On Windows, wchar_t is 16 bits, while on Linux, it's 32 bits
#ifdef WideCharIsUTF16
            /*
            * All according to the formula from https://en.wikipedia.org/wiki/UTF-16#Examples
            * For example, let's consider 0xD801 0xDC37
            * For the high surrogate:
            * 1. Subtract 0xD800 from the high surrogate 0xD801: (0x0001)
            * 2. Multiply by 0x400: 0x0001 * 0x400 == 0x0400
            * For the low surrogate:
            * 1. Subtract 0xDC00 from the low surrogate 0xDC37: 00110111 (0x37)
            * Final step:
            * 1. Add the obtained results: 0x0400 + 0x37 == 0x0437
            * 2. Add 0x10000: 0x0437 + 0x10437
            * Voilà! We have a UTF-32 character, which can now be converted to UTF-8 (see ConvertUTF8ToWideString)!
            */
            // Surrogates for four-byte characters
            // High surrogate: U+D800 - U+DBFF
            // Low surrogate: U+DC00 - U+DFFF
            else if (wchar >= 0xD800 && wchar <= 0xDBFF) {
                codePoint = ((wchar - 0xD800) * 0x400);
            }
            else if (wchar >= 0xDC00 && wchar <= 0xDFFF) {
                if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                    throw std::invalid_argument("Invalid UTF-16 sequence: unexpected low surrogate");
                }
                codePoint += (wchar - 0xDC00);
                codePoint += 0x10000;
                if (codePoint > 0x10FFFF) {
                    throw std::invalid_argument("Invalid UTF-16 sequence: low surrogate is out of range");
                }
                out.push_back(static_cast<char8_t>((codePoint >> 18) | 0xF0));
                out.push_back(static_cast<char8_t>(((codePoint >> 12) & 0x3F) | 0x80));
                out.push_back(static_cast<char8_t>(((codePoint >> 6) & 0x3F) | 0x80));
                out.push_back(static_cast<char8_t>((codePoint & 0x3F) | 0x80));
            }
#endif
            /*
            * Exactly the same as for two-byte characters,
            * with an additional byte taken into account
            */
            // Three-byte characters
            else {
                out.push_back(static_cast<char8_t>((wchar >> 12) | 0xE0));
                out.push_back(static_cast<char8_t>(((wchar >> 6) & 0x3F) | 0x80));
                out.push_back(static_cast<char8_t>((wchar & 0x3F) | 0x80));
            }
            // On Windows, wchar_t is 16 bits, while on Linux, it's 32 bits
#ifdef WideCharIsUTF32
            // Four-byte characters
            else if (wchar <= 0x10FFFF) {
                out.push_back(static_cast<char8_t>((wchar >> 18) | 0xF0));
                out.push_back(static_cast<char8_t>(((wchar >> 12) & 0x3F) | 0x80));
                out.push_back(static_cast<char8_t>(((wchar >> 6) & 0x3F) | 0x80));
                out.push_back(static_cast<char8_t>((wchar & 0x3F) | 0x80));
            }
#endif
        }
        return out;
    }

    std::wstring UTF8ToWideStr(const std::u8string_view in) {
        std::wstring out;
        // Preallocate memory for the "out" string to reduce the number of reallocations
        // 1 utf8 byte  (in.size() / 2 == 0 (exception)) -- 1 utf16 byte
        // 2 utf8 bytes (in.size() / 2 == 1)         -- 1 utf16 byte
        // 3 utf8 bytes (in.size() / 2 == 1{.5})     -- 1 utf16 byte
        // 4 utf8 bytes (in.size() / 2 == 2)         -- 2 utf16 bytes
        out.reserve(in.size() > 1 ? in.size() / 2 : 1);
        for (size_t i = 0; i < in.size(); ) {
            // If the first bit is zero, then it's a single-byte character
            if ((in[i] & 0x80) == 0) {
                out.push_back(static_cast<wchar_t>(in[i]));
                i += 1;
            }
            // If the first three bits are 110, then it's a two-byte character
            else if ((in[i] & 0xE0) == 0xC0) {
                /*
                * Example: we have 2 bytes -- 11010001(in[i]) and 10001000(in[i+1])
                * Apply a mask 00011111 to the first byte to replace
                * the top three bits with zeros, resulting in 00010001(in[i])
                *
                * Apply a mask 00111111 to the second byte to fill
                * the top two bits with zeros, resulting in 00001000(in[i+1])
                *
                * Shift the first byte inwards by 6 bits to make room for
                * the bits we need from the second byte in[i+1], resulting in
                * 00000000 00000000 00000100 01000000 (inside in)
                *
                * Simply add the remaining bits from the second byte in[i+1]
                * to this variable to get 00000000 00000000 00000100 01001000
                * Voilà! We have a UTF32 character!
                */
                out.push_back(static_cast<wchar_t>(((in[i] & 0x1F) << 6) | (in[i + 1] & 0x3F)));
                i += 2;
            }
            // If the first four bits are 1110, then it's a three-byte character
            else if ((in[i] & 0xF0) == 0xE0) {
                /*
                * Same as above, but the mask for the first byte replaces the top four bits with zeros,
                * and everything shifts considering one more byte
                */
                out.push_back(static_cast<wchar_t>(((in[i] & 0x0F) << 12) | ((in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F)));
                i += 3;
            }
            // If the first five bits are 11110, then it's a four-byte character
            else if ((in[i] & 0xF8) == 0xF0) {
                // On Windows, wchar_t is 16 bits, while on Linux, it's 32 bits
#ifdef _WIN32
                /*
                * Same as above, but the mask for the first byte replaces the top five bits with zeros,
                * and everything shifts considering one more byte
                */
                uint32_t u32 = ((in[i] & 0x07) << 24) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F);
                /*
                * Everything is done according to the formula from here:
                * https://en.wikipedia.org/wiki/UTF-16#Examples
                * Example: We have 0x00010437 in UTF32
                * First subtract 0x10000 and get 0x00000437
                * Shift right by 10 and add 0xD800 to find the high surrogate
                * Shift the high surrogate left by 16 to make room for the low surrogate
                * Take the lowest 10 bits (remainder after dividing by 0x400) and add 0xDC00
                * Simply combine these two bytes together
                * Voilà! UTF16 surrogate pair!
                */
                out.push_back(static_cast<wchar_t>(((u32 - 0x10000) >> 10) + 0xD800));
                out.push_back(static_cast<wchar_t>((u32 % 0x400) + 0xDC00));
#else
                out.push_back(static_cast<wchar_t>(((in[i] & 0x07) << 24) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F)));
#endif
                i += 4;
            }
            // This is not a UTF8 character
            else {
                throw std::invalid_argument("Invalid character");
                break;
            }
        }
        return out;
    }
}

