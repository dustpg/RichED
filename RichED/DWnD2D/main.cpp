#include "../RichED/ed_txtplat.h"
#include "../RichED/ed_txtdoc.h"
#include "../RichED/ed_txtcell.h"

// windows
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <minwindef.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// c++
#include <string>
#include <vector>



enum { WINDOW_WIDTH = 1280, WINDOW_HEIGHT = 720 };
static const wchar_t WINDOW_TITLE[] = L"RichED - DWnD2D";
enum { ECODE_FAILED = -1, ECODE_OOM = -2, ECODE_DRAW = -3, ECODE_RESIZE = -4 };

enum { DEF_FONT_SIZE = 32 };

enum { CTRL_X = 100, CTRL_Y = 100,  };

float ctrl_w = 300, ctrl_h = 300;

static const wchar_t* const FONT_NAME_LIST[] = {
     L"Arial",
     L"Kunstler Script",
     L"KaiTi",
     L"SimSun",
};




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
    return { str, str + len };
}


struct DWnD2DDataField {
    HWND                    hwnd;
    // DW
    IDWriteFactory1*        dw1_factory;
    IDWriteTextFormat*      dw1_deffont;
    // D2D
    ID2D1Factory*           d2d_factory;
    ID2D1SolidColorBrush*   d2d_black;
    ID2D1SolidColorBrush*   d2d_brush;
    ID2D1SolidColorBrush*   d2d_cell1;
    ID2D1SolidColorBrush*   d2d_cell2;
    ID2D1HwndRenderTarget*  d2d_rendertarget;
};

struct WinDWnD2D final : IEDTextPlatform {
    // ctor
    WinDWnD2D() noexcept {}
    // on out of memory, won't be called on ctor
    auto OnOOM(uint32_t retry_count) noexcept->HandleOOM override { std::exit(ECODE_OOM); return OOM_NoReturn; }
    // value changed
    void ValueChanged(Changed) noexcept override;
    // is valid password
    bool IsValidPassword(char32_t ch) noexcept override { return ch < 128; }
    // generate text
    void GenerateText(void* string, U16View view) noexcept override {
        auto& obj = *reinterpret_cast<std::u16string*>(string);
        try { obj.append(view.first, view.second); }
        catch (...) {}
    }
    // recreate context
    void RecreateContext(CEDTextCell& cell) noexcept override;
    // delete context
    void DeleteContext(CEDTextCell&) noexcept override;
    // draw context
    void DrawContext(CEDTextCell&, unit_t y) noexcept override;
    // hittest the cell
    auto HitTest(CEDTextCell&, unit_t offset) noexcept->CellHitTest override;
    // cm
    auto GetCharMetrics(CEDTextCell&, uint32_t offset) noexcept->CharMetrics override;
#ifndef NDEBUG
    // debug output
    void DebugOutput(const char*) noexcept override;
#endif



    // win proc
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    // init
    bool Init(HWND) noexcept;
    // clear
    void Clear() noexcept;
    // resize
    void Resize(uint32_t width, uint32_t height) noexcept;
    // render
    void Render() noexcept;


    uint32_t        cell_draw_i = 0;
    uint32_t        caret_blink_i = 0;
    uint32_t        caret_blink_time = 500;
    char16_t        save_u16 = 0;

    // doc
    std::aligned_storage<sizeof(CEDTextDocument), alignof(CEDTextDocument)>
        ::type      doc;

    // data
    DWnD2DDataField data;

    auto& Doc() noexcept { return *reinterpret_cast<CEDTextDocument*>(&doc); }

} g_platform;



template<class Interface>
inline void SafeRelease(Interface *&pInterfaceToRelease) noexcept {
    if (pInterfaceToRelease != nullptr) {
        pInterfaceToRelease->Release();
        pInterfaceToRelease = nullptr;
    }
}

int main() {
    MemoryLeakDetector leak;
    // DPIAware
    ::SetProcessDPIAware();
    // 注册窗口
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WinDWnD2D::WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(LONG_PTR);
    wcex.hInstance = ::GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"DemoWindowClass";
    wcex.hIcon = nullptr;
    ::RegisterClassExW(&wcex);
    // 计算窗口大小
    RECT window_rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    DWORD window_style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&window_rect, window_style, FALSE);
    window_rect.right -= window_rect.left;
    window_rect.bottom -= window_rect.top;
    window_rect.left = (::GetSystemMetrics(SM_CXFULLSCREEN) - window_rect.right) / 2;
    window_rect.top = (::GetSystemMetrics(SM_CYFULLSCREEN) - window_rect.bottom) / 2;
    // 创建窗口
    const auto hwnd = ::CreateWindowExW(
        0,
        wcex.lpszClassName, WINDOW_TITLE, window_style,
        window_rect.left, window_rect.top, window_rect.right, window_rect.bottom,
        0, 0, ::GetModuleHandleW(nullptr), nullptr
    );
    if (!hwnd) return ECODE_FAILED;
    if (g_platform.Init(hwnd)) {
        ::ShowWindow(hwnd, SW_NORMAL);
        ::UpdateWindow(hwnd);
        const auto blink = ::GetCaretBlinkTime();
        g_platform.caret_blink_time = blink;
        const auto id = reinterpret_cast<uintptr_t>(&g_platform);
        ::SetTimer(hwnd, id, blink, nullptr);
        MSG msg = { 0 };
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    g_platform.Clear();
    return 0;
}

namespace FAKE_IID {

    /// <summary>
    /// The iid identifier write factory1
    /// </summary>
    const GUID IID_IDWriteFactory1 = {
        0x30572f99, 0xdac6, 0x41db,
        { 0xa1, 0x6e, 0x04, 0x86, 0x30, 0x7e, 0x60, 0x6a }
    };
    /// <summary>
    /// The iid identifier write text layout1
    /// </summary>
    const GUID IID_IDWriteTextLayout1 = {
        0x9064d822, 0x80a7, 0x465c, {
        0xa9, 0x86, 0xdf, 0x65, 0xf7, 0x8b, 0x8f, 0xeb }
    };
}


bool WinDWnD2D::Init(HWND hwnd) noexcept {
    auto& d = this->data;
    d.hwnd = hwnd;
    HRESULT hr = S_OK;
    if (SUCCEEDED(hr)) {
        DocInitArg arg = {
            0, Flag_RichText | Flag_MultiLine, '*', 0xffffff, 0,
            VAlign_Baseline, Mode_SpaceOrCJK,
            { DEF_FONT_SIZE, 0, 0, Effect_None } 
        };
        std::memset(&doc, 0xcc, sizeof(doc));
        new(&doc) CEDTextDocument{ *this, arg };
        hr = arg.code;
    }
    // 初文字
    if (SUCCEEDED(hr)) {
        const auto string = u"Hello, world!\r\n泥壕世界!\r\nHello, world!"_red;
        //this->Doc().InsertText({ 0, 0 }, string);
        //this->Doc().InsertRuby({ 0, 1 }, U'汉', u"hàn"_red);
        //this->Doc().InsertRuby({ 0, 1 }, U'漢', u"かん"_red);
        //this->Doc().InsertText({ 0, 4 }, u"kankankan"_red);
        //this->Doc().InsertText({ 0, 0 }, u"汉hàn"_red);

        //for (int i = 0; i != 12; ++i)
            //this->Doc().InsertText({ 0, 0 }, u"汉"_red);
        //this->Doc().InsertText({ 0, 0 }, u"汉\n"_red);
            //this->Doc().InsertRuby({ 0, 0 }, U'汉', u"hàn"_red);


        this->Doc().InsertText({ 0, 0 }, u"国人发明的"_red);
        this->Doc().InsertRuby({ 0, 0 }, U'韩', u"宇宙"_red);
        this->Doc().InsertText({ 0, 0 }, u"字没准儿是"_red);
        //this->Doc().InsertText({ 0, 0 }, u"😂😂😂😂😂"_red);
        this->Doc().InsertRuby({ 0, 0 }, U'汉', u"hàn"_red);
    }
    // 创建DWrite工厂
    if (SUCCEEDED(hr)) {
        hr = ::DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            FAKE_IID::IID_IDWriteFactory1,
            reinterpret_cast<IUnknown**>(&d.dw1_factory)
        );
    }
    // 创建基础字体
    if (SUCCEEDED(hr)) {
        hr = d.dw1_factory->CreateTextFormat(
            FONT_NAME_LIST[0], nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            DEF_FONT_SIZE,
            L"",
            &d.dw1_deffont
        );
    }
    if (SUCCEEDED(hr)) {
        d.dw1_deffont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    if (SUCCEEDED(hr)) {
        hr = ::D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, 
            &d.d2d_factory
        ); 
    }
    if (SUCCEEDED(hr)) {
        RECT rc; ::GetClientRect(hwnd, &rc);
        const D2D1_SIZE_U size{
            uint32_t(rc.right - rc.left),
            uint32_t(rc.bottom - rc.top)
        };
        hr = d.d2d_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, size),
            &d.d2d_rendertarget
        );
    }
    if (SUCCEEDED(hr)) {
        void* ptr = nullptr;
        d.d2d_rendertarget->QueryInterface(IID_ID2D1DeviceContext, &ptr);
        if (const auto obj = static_cast<ID2D1DeviceContext*>(ptr)) {
            obj->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
            obj->Release();
        }
    }
    if (SUCCEEDED(hr)) {
        hr = d.d2d_rendertarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            &d.d2d_brush
        );
    }
    if (SUCCEEDED(hr)) {
        hr = d.d2d_rendertarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            &d.d2d_black
        );
    }
    if (SUCCEEDED(hr)) {
        hr = d.d2d_rendertarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Green, 0.25f),
            &d.d2d_cell1
        );
    }
    if (SUCCEEDED(hr)) {
        hr = d.d2d_rendertarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Blue, 0.25f),
            &d.d2d_cell2
        );
    }
    return SUCCEEDED(hr);
}


void WinDWnD2D::Clear() noexcept {
    this->Doc().~CEDTextDocument();
    ::SafeRelease(this->data.dw1_deffont);
    ::SafeRelease(this->data.dw1_factory);
    ::SafeRelease(this->data.d2d_black);
    ::SafeRelease(this->data.d2d_brush);
    ::SafeRelease(this->data.d2d_cell1);
    ::SafeRelease(this->data.d2d_cell2);
    ::SafeRelease(this->data.d2d_rendertarget);
    ::SafeRelease(this->data.d2d_factory);
}


LRESULT WinDWnD2D::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept {

#define is_high_surrogate(ch) (((ch) & 0xFC00) == 0xD800)
#define is_low_surrogate(ch) (((ch) & 0xFC00) == 0xDC00)
    const auto char16x2_to_char32 = [](char16_t lead, char16_t trail) {
        assert(is_high_surrogate(lead) && "illegal utf-16 char");
        assert(is_low_surrogate(trail) && "illegal utf-16 char");
        return (char32_t)((lead - 0xD800) << 10 | (trail - 0xDC00)) + (0x10000);
    };

    bool handled = false;
    LRESULT result = 0;
    switch (message)
    {
        bool ctrl, shift;
        int16_t x, y;
        PAINTSTRUCT ps;
        char32_t ch;
    case WM_SIZE:
        g_platform.Resize(LOWORD(lParam), HIWORD(lParam));
        handled = true;
        result = 0;
        break;
    case WM_PAINT:
    case WM_DISPLAYCHANGE:
        ::BeginPaint(hwnd, &ps);
        g_platform.Render();
        ::EndPaint(hwnd, &ps);
        handled = true;
        result = 0;
        break;
    case WM_TIMER:
        g_platform.caret_blink_i ^= 1;
        ::InvalidateRect(hwnd, nullptr, false);
        break;
    case WM_LBUTTONDOWN:
        x = int16_t(LOWORD(lParam)) - CTRL_X;
        y = int16_t(HIWORD(lParam)) - CTRL_Y;
        if (x >= 0 && y >= 0 && x < int16_t(ctrl_w) && y < int16_t(ctrl_h)) {
            g_platform.Doc().GuiLButtonDown({ (float)x, (float)y }, !!(wParam & MK_SHIFT));
        }
        break;
    case WM_LBUTTONUP:
        x = int16_t(LOWORD(lParam)) - CTRL_X;
        y = int16_t(HIWORD(lParam)) - CTRL_Y;
        if (x >= 0 && y >= 0 && x < int16_t(ctrl_w) && y < int16_t(ctrl_h)) {
            g_platform.Doc().GuiLButtonUp({ (float)x, (float)y });
        }
        break;
    case WM_CHAR:

        ch = static_cast<char32_t>(wParam);
        if (is_low_surrogate(char16_t(wParam))) {
            ch = char16x2_to_char32(g_platform.save_u16, char16_t(wParam));
        }
        else if (is_high_surrogate(char16_t(wParam))) {
            g_platform.save_u16 = char16_t(wParam);
            break;
        }
        // 有效字符:  \b 之类的控制符不算
        if (ch >= 0x20 || ch == '\t') g_platform.Doc().GuiChar(ch);
        break;
    case WM_KEYDOWN:
        handled = true;
        result = 0;
        ctrl = (::GetKeyState(VK_CONTROL) & 0x80) != 0;
        shift = (::GetKeyState(VK_CONTROL) & 0x80) != 0;
        switch (wParam)
        {
        default:
            handled = false;
            break;
        case VK_LEFT:
            g_platform.Doc().GuiLeft(ctrl, shift);
            break;
        case VK_UP:
            g_platform.Doc().GuiUp(ctrl, shift);
            break;
        case VK_RIGHT:
            g_platform.Doc().GuiRight(ctrl, shift);
            break;
        case VK_DOWN:
            g_platform.Doc().GuiDown(ctrl, shift);
            break;
        //case 'P':
        //    g_platform.Doc().InsertText({ 0, 1000 }, u"red \nblur"_red);
        //    break;
        case VK_BACK:
            g_platform.Doc().GuiBackspace(ctrl);
            break;
        case VK_DELETE:
            g_platform.Doc().GuiDelete(ctrl);
            break;
        case VK_F1:
            g_platform.Doc().SetFontSize({ 0, 7 }, { 0, 8 }, 20.f);
            break;
        case VK_F2:
            g_platform.Doc().SetFontColor({ 0, 7 }, { 0, 8 }, 0xff0000);
            break;
        case VK_F3:
            g_platform.Doc().SetFontName({ 0, 7 }, { 0, 8 }, 2);
            break;
        case VK_F4:
            g_platform.Doc().SetUnerline({ 0, 4 }, { 0, 8 }, CEDTextDocument::Set_Change);
            break;
        case VK_F5:
            ctrl_w += 10.f;
            //ctrl_h += 10.f;
            g_platform.Doc().Resize({ ctrl_w, ctrl_h });
            break;
        case VK_F6:
            if (ctrl_w > 20.f) {
                ctrl_w -= 10.f;
                //ctrl_h -= 10.f;
                g_platform.Doc().Resize({ ctrl_w, ctrl_h });
            }
            break;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        handled = true;
        result = 1;
        break;
    }

    if (!handled) result = ::DefWindowProcW(hwnd, message, wParam, lParam);
    return result;
}



/// <summary>
/// Recreates the context.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="prev">The previous.</param>
/// <returns></returns>
void WinDWnD2D::RecreateContext(CEDTextCell& cell) noexcept {
    // 释放之前的布局
    const auto prev_layout = reinterpret_cast<IDWriteTextLayout*>(cell.ctx.context);
    cell.ctx.context = nullptr;
    if (prev_layout) prev_layout->Release();
    // 利用字符串创建DWrite文本布局
    const auto& str = cell.GetString();
    // 空的场合
    if (!str.length) {
        if (cell.GetMetaInfo().dirty) {
            cell.metrics.width = 0;
            const auto h = cell.GetRichED().size;
            cell.metrics.ar_height = h;
            cell.metrics.dr_height = 0;
            cell.metrics.bounding.left = 0;
            cell.metrics.bounding.top = 0;
            cell.metrics.bounding.right = 0;
            cell.metrics.bounding.bottom = h;
            cell.AsClean();
        }
        return;
    }
    const auto dw1_factory = this->data.dw1_factory;
    // 创建新的文本格式
    const auto fmt = [=, &cell]() noexcept -> IDWriteTextFormat* {
        // 富文本模式
        if (this->Doc().GetInfo().flags & Flag_RichText) {
            const auto& riched = cell.GetRichED();
            IDWriteTextFormat* fmt = nullptr;
            dw1_factory->CreateTextFormat(
                FONT_NAME_LIST[riched.name],
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                riched.size,
                L"",
                &fmt
            );
            if (fmt) {
                //fmt->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
                //fmt->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
                fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                return fmt;
            }
            // 懒得错误处理, 使用默认字体格式
        }
        //  使用默认字体格式
        this->data.dw1_deffont->AddRef();
        return this->data.dw1_deffont;
    }();
    // 创建新的布局
    static_assert(sizeof(wchar_t) == sizeof(str.data[0]), "must be same!");
    auto hr = this->data.dw1_factory->CreateTextLayout(
        reinterpret_cast<const wchar_t*>(str.data), str.length,
        fmt, 0, 0, reinterpret_cast<IDWriteTextLayout**>(&cell.ctx.context)
    );
    // 测量CELL
    if (cell.GetMetaInfo().dirty) {
        const auto layout = reinterpret_cast<IDWriteTextLayout*>(cell.ctx.context);
        DWRITE_TEXT_METRICS dwtm;
        DWRITE_LINE_METRICS dwlm;
        DWRITE_OVERHANG_METRICS dwom;
        if (SUCCEEDED(hr)) {
            hr = layout->GetMetrics(&dwtm);
        }
        if (SUCCEEDED(hr)) {
            hr = layout->GetOverhangMetrics(&dwom);
        }
        if (SUCCEEDED(hr)) {
            uint32_t count = 1;
            hr = layout->GetLineMetrics(&dwlm, 1, &count);
        }
        if (SUCCEEDED(hr)) {
            cell.metrics.width = dwtm.width;
            this->Doc().VAlignHelperH(dwlm.baseline, dwlm.height, cell.metrics);
            cell.metrics.bounding.left = -dwom.left;
            cell.metrics.bounding.top = -dwom.top;
            cell.metrics.bounding.right = dwom.right;
            cell.metrics.bounding.bottom = dwom.bottom;

            //cell.metrics.bounding.left = 0;
            //cell.metrics.bounding.top = 0;
            //cell.metrics.bounding.right = dwtm.width;
            //cell.metrics.bounding.bottom = dwlm.height;
        }
        // CLEAN!
        cell.AsClean();
    }
    // 释放
    fmt->Release();
}

/// <summary>
/// Deletes the context.
/// </summary>
/// <param name="cell">The cell.</param>
/// <returns></returns>
void WinDWnD2D::DeleteContext(CEDTextCell& cell) noexcept {
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        ptr->Release();
    }
    cell.ctx.context = nullptr;
}


void WinDWnD2D::DrawContext(CEDTextCell& cell, unit_t baseline) noexcept {
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    D2D1_POINT_2F point;
    point.x = cell.metrics.pos + cell.metrics.offset.x;
    // 失败则不渲染
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        const auto brush = this->data.d2d_brush;
        // 富文本
        if (this->Doc().GetInfo().flags & Flag_RichText) {
            brush->SetColor(D2D1::ColorF(cell.GetRichED().color));
        }
        // 渲染CELL
        point.y = baseline - cell.metrics.ar_height + cell.metrics.offset.y;
        const auto renderer = this->data.d2d_rendertarget;
#ifndef NDEBUG
        const auto dcb = (++cell_draw_i & 1)[&data.d2d_cell1];
        D2D1_RECT_F cell_rect;
        cell_rect.left = point.x + cell.metrics.bounding.left;
        cell_rect.top = point.y + cell.metrics.bounding.top;
        cell_rect.right = point.x + cell.metrics.bounding.right;
        cell_rect.bottom = point.y + cell.metrics.bounding.bottom;
        renderer->FillRectangle(&cell_rect, dcb);
#endif
        renderer->DrawTextLayout(point, ptr, brush);
    }
    // 下划线
    if (cell.GetRichED().effect & Effect_Underline) {
        point.y = baseline + cell.metrics.dr_height + cell.metrics.offset.y;
        const auto renderer = this->data.d2d_rendertarget;
        auto point2 = point; point2.x += cell.metrics.width;
        const auto brush = this->data.d2d_brush;
        renderer->DrawLine(point, point2, brush);
    }
}


/// <summary>
/// Values the changed.
/// </summary>
/// <param name="changed">The changed.</param>
/// <returns></returns>
void WinDWnD2D::ValueChanged(Changed changed) noexcept {
    switch (changed)
    {
    case RichED::IEDTextPlatform::Changed_View:
        ::InvalidateRect(this->data.hwnd, nullptr, false);
        break;
    case RichED::IEDTextPlatform::Changed_Selection:
        break;
    case RichED::IEDTextPlatform::Changed_Caret:
        ::SetTimer(data.hwnd, reinterpret_cast<uintptr_t>(this), caret_blink_time, nullptr);
        this->caret_blink_i = 1;
        ::InvalidateRect(this->data.hwnd, nullptr, false);
        break;
    case RichED::IEDTextPlatform::Changed_Text:
        break;
    case RichED::IEDTextPlatform::Changed_EstimatedWidth:
        break;
    case RichED::IEDTextPlatform::Changed_EstimatedHeight:
        break;
    }
}


/// <summary>
/// Hits the test.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::HitTest(CEDTextCell& cell, unit_t offset) noexcept->CellHitTest {
    CellHitTest rv = { 0 };
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    // 失败则不获取
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        BOOL hint = false, inside = false;
        DWRITE_HIT_TEST_METRICS htm;
        ptr->HitTestPoint(offset, 0, &hint, &inside, &htm);
        rv.pos = htm.textPosition;
        rv.trailing = hint;
        rv.length = htm.length;
    }
    return rv;
}

/// <summary>
/// Gets the character metrics.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::GetCharMetrics(CEDTextCell& cell, uint32_t pos) noexcept -> CharMetrics {
    CharMetrics cm = { 0 };
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    // 失败则不获取
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        if (pos == cell.GetString().length) {
            cm.offset = cell.metrics.width;
        }
        else try {
            std::vector<DWRITE_CLUSTER_METRICS> buf;
            const uint32_t size = pos + 1;
            buf.resize(size);
            uint32_t max_count = 0;
            ptr->GetClusterMetrics(buf.data(), size, &max_count);
            // 遍历位置
            const auto data_ptr = buf.data();
            const auto data_len = max_count;
            float width = 0, last_width = 0;
            uint32_t char_index = 0;
            // 防止万一, 加上 [i != data_len]
            for (uint32_t i = 0; i != data_len; ++i) {
                const auto&x = data_ptr[i];
                width += last_width = x.width;
                // 防止万一用>=
                if (char_index >= pos) break;
                char_index += x.length;
            }
            // 写回返回值
            cm.width = last_width;
            cm.offset = width - last_width;
        }
        catch (...) {}
    }
    return cm;
}

#ifndef NDEBUG
/// <summary>
/// Debugs the output.
/// </summary>
/// <param name="text">The text.</param>
/// <returns></returns>
void WinDWnD2D::DebugOutput(const char* text) noexcept {
    std::printf("[RichED Debug] %s\n", text);
}

#endif


/// <summary>
/// Resizes the specified width.
/// </summary>
/// <param name="width">The width.</param>
/// <param name="height">The height.</param>
/// <returns></returns>
void WinDWnD2D::Resize(uint32_t width, uint32_t height) noexcept {
    this->Doc().Resize({ ctrl_w, ctrl_h });
    const auto rendertarget = this->data.d2d_rendertarget;
    const auto hr = rendertarget->Resize({ width, height });
    if (FAILED(hr)) std::exit(ECODE_RESIZE);
}


/// <summary>
/// Renders this instance.
/// </summary>
/// <returns></returns>
void WinDWnD2D::Render() noexcept {
    this->Doc().BeforeRender();
    this->cell_draw_i = 0;

    const auto renderer = this->data.d2d_rendertarget;
    const auto brush = this->data.d2d_black;
    if (renderer->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;
    renderer->BeginDraw();
    renderer->Clear(D2D1::ColorF(0x66ccff));
    renderer->SetTransform(D2D1::Matrix3x2F::Translation(CTRL_X, CTRL_Y));
    renderer->DrawRectangle({0, 0, ctrl_w, ctrl_h }, brush);
    this->Doc().Render();
    if (this->caret_blink_i) {
        const auto caret = this->Doc().GetCaret();

        constexpr float custom_width = 2.f;
        constexpr float custom_height_ratio = 0.9f;

        const float custom_height = caret.height * custom_height_ratio;
        const float custom_top = caret.y
            + (1.f - custom_height_ratio) * 0.5f
            * caret.height
            ;

        D2D1_RECT_F rect = {
            caret.x - custom_width * 0.5f, 
            custom_top,
            caret.x + custom_width * 0.5f,
            custom_top + custom_height
        };
        renderer->FillRectangle(rect, this->data.d2d_black);
    }
    const auto hr = renderer->EndDraw();

    if (FAILED(hr)) std::exit(ECODE_DRAW);
}