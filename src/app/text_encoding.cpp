#include "text_encoding.h"

#include <windows.h>

#include <cstring>

namespace sheetpad {

namespace {

std::string AnsiToUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return WideToUtf8(wide);
}

}  // namespace

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string DecodeFileBytes(std::string_view bytes, Encoding& encoding) {
    const auto at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 2 && ((at(0) == 0xFF && at(1) == 0xFE) || (at(0) == 0xFE && at(1) == 0xFF))) {
        const bool bigEndian = at(0) == 0xFE;
        std::wstring wide((bytes.size() - 2) / 2, L'\0');
        for (std::size_t i = 0; i < wide.size(); ++i) {
            const unsigned char lo = at(2 + i * 2 + (bigEndian ? 1 : 0));
            const unsigned char hi = at(2 + i * 2 + (bigEndian ? 0 : 1));
            wide[i] = static_cast<wchar_t>(lo | (hi << 8));
        }
        encoding = Encoding::Utf16Le;
        return WideToUtf8(wide);
    }
    if (bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF) {
        encoding = Encoding::Utf8Bom;
        return std::string(bytes.substr(3));
    }

    encoding = Encoding::Utf8;
    if (IsValidUtf8(bytes)) {
        return std::string(bytes);
    }
    return AnsiToUtf8(bytes);
}

std::string EncodeFileText(std::string_view utf8, Encoding encoding) {
    switch (encoding) {
    case Encoding::Utf8Bom:
        return std::string("\xEF\xBB\xBF") + std::string(utf8);
    case Encoding::Utf16Le: {
        const std::wstring wide = Utf8ToWide(utf8);
        std::string out("\xFF\xFE", 2);
        out.resize(2 + wide.size() * sizeof(wchar_t));
        std::memcpy(out.data() + 2, wide.data(), wide.size() * sizeof(wchar_t));
        return out;
    }
    case Encoding::Utf8:
        break;
    }
    return std::string(utf8);
}

}  // namespace sheetpad
