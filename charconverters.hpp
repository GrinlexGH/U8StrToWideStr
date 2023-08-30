#pragma once

#include <string>

#define WideCharIsUTF16

namespace CharConverters
{
    std::u8string WideStrToUTF8(const std::wstring_view in);
    std::wstring UTF8ToWideStr(const std::u8string_view in);
}

