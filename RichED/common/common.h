// riched
#include "../RichED/ed_txtplat.h"
#include "../RichED/ed_txtdoc.h"
#include "../RichED/ed_txtcell.h"

// windows
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <minwindef.h>
// c++
#include <string>
#include <vector>



/// <summary>
/// Copies the text to clipboard.
/// </summary>
/// <param name="view">The view.</param>
/// <returns></returns>
static bool CopyTextToClipboard(const std::wstring& view) noexcept {
    static_assert(sizeof(char16_t) == sizeof(wchar_t), "WINDOWS");
    // 申请全局内存
    constexpr size_t sz = sizeof(char16_t);
    const size_t len = sz * ((view.end() - view.begin()) + 1);
    const auto handle = ::GlobalAlloc(GMEM_MOVEABLE, len);
    if (handle) {
        // 准备解锁写入
        if (const auto str = reinterpret_cast<char16_t*>(::GlobalLock(handle))) {
            std::memcpy(str, view.data(), len - sz);
            str[view.end() - view.begin()] = 0;
            ::GlobalUnlock(handle);
        }
    }
    else return false;
    // 待返回值
    bool rv = false;
    // 打开任务剪切板
    if (::OpenClipboard(nullptr)) {
        ::EmptyClipboard();
        if (::SetClipboardData(CF_UNICODETEXT, handle)) rv = true;
        else ::GlobalFree(handle);
        ::CloseClipboard();
    }
    return rv;
}


/// <summary>
/// Pastes the text to clipboard.
/// </summary>
/// <param name="str">The string.</param>
/// <returns></returns>
static bool PasteTextToClipboard(std::wstring& str) noexcept {
    str.clear();
    bool rv = false;
    // 打开任务剪切板
    if (::OpenClipboard(nullptr)) {
        const auto handle = ::GetClipboardData(CF_UNICODETEXT);
        // 解锁写入
        if (const auto ptr = ::GlobalLock(handle)) {
            const auto len = ::GlobalSize(handle) / sizeof(char16_t) - 1;
            const auto text = reinterpret_cast<const char16_t*>(ptr);
            const auto real = len;
            try { str.assign(text, text + real); }
            catch (...) { return false; }
            rv = true;
            ::GlobalUnlock(handle);
        }
        ::CloseClipboard();
    }
    return rv;
}





struct MemoryLeakDetector {
#if !defined(NDEBUG) && defined(_MSC_VER)
    // mem state
    _CrtMemState memstate[3];
    // ctor
    MemoryLeakDetector() noexcept { ::_CrtMemCheckpoint(memstate + 0); }
    // dtor
    ~MemoryLeakDetector() noexcept {
        ::_CrtMemCheckpoint(memstate + 1);
        if (::_CrtMemDifference(memstate + 2, memstate + 0, memstate + 1)) {
            ::_CrtDumpMemoryLeaks();
            assert(!"OOps! Memory leak detected");
        }
    }
#else
    ~MemoryLeakDetector() noexcept {}
#endif
};


using namespace RichED;


inline U16View operator""_red(const char16_t* str, size_t len) noexcept {
    return { str, str + len }; }

