#include "../common/common.h"

// DWnD2D
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")



enum { WINDOW_WIDTH = 1280, WINDOW_HEIGHT = 720 };
static const wchar_t WINDOW_TITLE[] = L"RichED - DWnD2D";
enum { ECODE_FAILED = -1, ECODE_OOM = -2, ECODE_DRAW = -3, ECODE_RESIZE = -4 };

enum { DEF_FONT_SIZE = 42, UPDATE_DELTA_TIME = 40 };

enum : uint32_t { PASSWORD_CHAR = U'😂' };

enum { CTRL_X = 100, CTRL_Y = 100,  };

float ctrl_w = 800, ctrl_h = 200;


enum : uintptr_t {
    TIMER_ID_NULL = 0,
    TIMER_ID_BLINK,
    TIMER_ID_UPDATE,
};

static const wchar_t* const FONT_NAME_LIST[] = {
     L"Arial",
     L"Kunstler Script",
     L"KaiTi",
     L"SimSun",
};


auto LoadBitmapFromFile(
    ID2D1RenderTarget* pRenderTarget,
    IWICImagingFactory* pIWICFactory,
    PCWSTR uri,
    ID2D1Bitmap** ppBitmap
) noexcept->HRESULT;

struct RED_RICHED_ALIGNED DWnD2DImageExtraInfo {
    wchar_t             uri[4096];

    auto as_info() noexcept { return reinterpret_cast<InlineInfo*>(this); }

    auto create_img(const wchar_t p[]) noexcept {
        const auto l = std::wcslen(p);
        int16_t bytelen16 = 0;
        if (l * sizeof(wchar_t) < sizeof(uri)) {
            const size_t bytelen = sizeof(wchar_t) * (l + 1);
            std::memcpy(this->uri, p, bytelen);
            assert(bytelen < 30000);
            bytelen16 = static_cast<int16_t>(bytelen);
        }
        return bytelen16;
    };
};

struct DWnD2DDataField {
    HWND                    hwnd;
    // WIC
    IWICImagingFactory*     wic_factory;
    // DW
    IDWriteFactory1*        dw1_factory;
    IDWriteTextFormat*      dw1_deffont;
    // D2D
    ID2D1Factory*           d2d_factory;
    ID2D1SolidColorBrush*   d2d_black;
    ID2D1SolidColorBrush*   d2d_border;
    ID2D1SolidColorBrush*   d2d_brush;
    ID2D1SolidColorBrush*   d2d_cell1;
    ID2D1SolidColorBrush*   d2d_cell2;
    ID2D1HwndRenderTarget*  d2d_rendertarget;
};

constexpr char16_t IME_BUF_LEN = 68;

struct WinDWnD2D final : IEDTextPlatform {
    // ctor
    WinDWnD2D() noexcept { ::memset(&this->data, 0, sizeof(this->data)); }
    // on out of memory, won't be called on ctor
    auto OnOOM(uint32_t retry_count, size_t) noexcept->HandleOOM override { std::exit(ECODE_OOM); return OOM_NoReturn; }
    // is valid password
    bool IsValidPassword(char32_t ch) noexcept override { 
        return true;
        //return ch >= 0x20 && ch < 0x7f;
    }
    // append text
    bool AppendText(CtxPtr ctx, U16View view) noexcept override {
        auto& obj = *reinterpret_cast<std::u16string*>(ctx);
        try { obj.append(view.first, view.second); return true; }
        catch (...) {}
        return false;
    }
    // write to file
    bool WriteToFile(CtxPtr ctx, const uint8_t data[], uint32_t len) noexcept override {
        return false;
    }
    // read from file
    bool ReadFromFile(CtxPtr ctx, uint8_t data[], uint32_t len) noexcept override {
        return false;
    }
    // recreate context
    void RecreateContext(CEDTextCell& cell) noexcept override;
    // recreate context image
    void RecreateContextImage(CEDTextCell& cell) noexcept;
    // delete context
    void DeleteContext(CEDTextCell&) noexcept override;
    // draw context
    void DrawContext(CtxPtr,CEDTextCell&, unit_t y) noexcept override;
    // map! 
    void Map(float[2]) noexcept;
    // map! 
    void Map(float a[2], float b[2]) noexcept { Map(a); Map(b); }
    // draw context image
    void DrawContextImage(CEDTextCell&, unit_t y) noexcept;
    // hittest the cell
    auto HitTest(CEDTextCell&, unit_t offset) noexcept->CellHitTest override;
    // hittest the object
    auto HitTestObject(CEDTextCell&, unit_t offset) noexcept->CellHitTest;
    // cm
    auto GetCharMetrics(CEDTextCell&, uint32_t offset) noexcept->CharMetrics override;
    // cm
    auto GetCharMetricsImage(CEDTextCell&, uint32_t offset) noexcept->CharMetrics ;
#ifndef NDEBUG
    // debug output
    void DebugOutput(const char*, bool high) noexcept override;
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
    void Update() noexcept;

    void DrawSelection() noexcept;
    // set ime pose
    void SetImePosition() noexcept;


    uint32_t        cell_draw_i     = 0;
    uint32_t        caret_blink_i   = 0;
    uint32_t        caret_blink_time = 500;
    char16_t        save_u16        = 0;
    uint16_t        ime_char_count  = 0;
    uint16_t        ime_char_index  = 0;
    bool            click_in_area   = false;
    bool            unused          = false;
    char16_t        ime_buf[IME_BUF_LEN];


    // doc
    std::aligned_storage<sizeof(CEDTextDocument), alignof(CEDTextDocument)>
        ::type      doc;

    // data
    DWnD2DDataField data;

    auto& Doc() noexcept { return *reinterpret_cast<CEDTextDocument*>(&doc); }

} g_platform;


void WinDWnD2D::Map(float point [2]) noexcept {
    auto& matrix = this->Doc().RefMatrix();
    const auto ptr = reinterpret_cast<RichED::Point*>(point);
    *ptr = matrix.DocToScreen(*ptr);
}


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
    ::CoInitialize(nullptr);
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
        //const auto id = reinterpret_cast<uintptr_t>(&g_platform);
        ::SetTimer(hwnd, TIMER_ID_BLINK, blink, nullptr);
        ::SetTimer(hwnd, TIMER_ID_UPDATE, UPDATE_DELTA_TIME, nullptr);
        MSG msg = { 0 };
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    g_platform.Clear();
    ::CoUninitialize();
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
            0, Direction_L2R, Direction_T2B,
            //0, Direction_T2B, Direction_R2L,
            Flag_RichText | Flag_MultiLine,
            //Flag_UsePassword | Flag_MultiLine,
            PASSWORD_CHAR, 0xffffff, 0,
            VAlign_Baseline, Mode_SpaceOrCJK,
            { DEF_FONT_SIZE, 0, 0, Effect_None } 
        };
        std::memset(&doc, 0xcc, sizeof(doc));
        new(&doc) CEDTextDocument{ *this, arg };
        hr = arg.code;
    }
    // 初文字
    if (SUCCEEDED(hr)) {
        DWnD2DImageExtraInfo info;
#if 0
        //this->Doc().InsertText({ 0, 0 }, u"😂😂😂😂😂"_red);
#else
        this->Doc().InsertText({ 0, 0 }, u"\r\n\r\nHello, World!\r\n泥壕世界!\n"_red);
        this->Doc().InsertInline({ 0, 0 }, *info.as_info(), info.create_img(L"../common/2.png"), Type_Image);
        this->Doc().InsertText({ 0, 0 }, u"国人发明的"_red);
        this->Doc().InsertRuby({ 0, 0 }, U'韩', u"宇宙"_red);
        this->Doc().InsertText({ 0, 0 }, u"字没准儿是"_red);
        //this->Doc().SetFontName({ 0, 1 }, { 0, 4 }, 2);
        this->Doc().SetFontSize({ 0, 3 }, { 0, 4 }, DEF_FONT_SIZE*3/5);
        this->Doc().InsertRuby({ 0, 0 }, U'汉', u"hàn"_red);
        this->Doc().InsertText({ 0, 0 }, u"Hello, World!\r\n泥壕世界!\n"_red);
#endif
    }
    // 创建 WIC 工厂.
    if (SUCCEEDED(hr)) {
        hr = ::CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory,
            reinterpret_cast<void**>(&d.wic_factory)
        );
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
            D2D1::ColorF(D2D1::ColorF::Black, 0.3f),
            &d.d2d_border
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
    ::SafeRelease(this->data.d2d_border);
    ::SafeRelease(this->data.d2d_brush);
    ::SafeRelease(this->data.d2d_cell1);
    ::SafeRelease(this->data.d2d_cell2);
    ::SafeRelease(this->data.d2d_rendertarget);
    ::SafeRelease(this->data.d2d_factory);
    ::SafeRelease(this->data.wic_factory);
}


LRESULT WinDWnD2D::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept {

#define is_high_surrogate(ch) (((ch) & 0xFC00) == 0xD800)
#define is_low_surrogate(ch) (((ch) & 0xFC00) == 0xDC00)
    const auto char16x2_to_char32 = [](char16_t lead, char16_t trail) {
        assert(is_high_surrogate(lead) && "illegal utf-16 char");
        assert(is_low_surrogate(trail) && "illegal utf-16 char");
        return (char32_t)((lead - 0xD800) << 10 | (trail - 0xDC00)) + (0x10000);
    };
    const auto msg_box_text = [hwnd](DocPoint a, DocPoint b) noexcept {
        std::wstring str;
        g_platform.Doc().GenText(&str, a, b);
        ::MessageBoxW(hwnd, str.c_str(), L"<GenText>", MB_OK);
    };


    bool handled = false;
    LRESULT result = 0;
    switch (message)
    {
        bool ctrl, shift, gui_op;
        int16_t x, y;
        PAINTSTRUCT ps;
        DocRange dr;
        DWnD2DImageExtraInfo info;
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
        switch (wParam)
        {
        case TIMER_ID_BLINK:
            g_platform.caret_blink_i ^= 1;
            ::InvalidateRect(hwnd, nullptr, false);
            break;
        case TIMER_ID_UPDATE:
            g_platform.Update();
            break;
        }
        handled = true;
        break;
    case WM_MOUSEMOVE:
        x = int16_t(LOWORD(lParam)) - CTRL_X;
        y = int16_t(HIWORD(lParam)) - CTRL_Y;
        if (g_platform.click_in_area && wParam & MK_LBUTTON) {
            g_platform.Doc().GuiLButtonHold({ (float)x, (float)y });
        }
        break;
    case WM_LBUTTONDOWN:
        x = int16_t(LOWORD(lParam)) - CTRL_X;
        y = int16_t(HIWORD(lParam)) - CTRL_Y;
        g_platform.click_in_area = false;
        if (x >= 0 && y >= 0 && x < int16_t(ctrl_w) && y < int16_t(ctrl_h)) {
            g_platform.click_in_area = true;
            g_platform.Doc().GuiLButtonDown({ (float)x, (float)y }, !!(wParam & MK_SHIFT));
        }
        break;
    //case WM_LBUTTONUP:
    //    x = int16_t(LOWORD(lParam)) - CTRL_X;
    //    y = int16_t(HIWORD(lParam)) - CTRL_Y;
    //    if (x >= 0 && y >= 0 && x < int16_t(ctrl_w) && y < int16_t(ctrl_h)) {
    //        g_platform.Doc().GuiLButtonUp({ (float)x, (float)y });
    //    }
    //    break;
    case WM_IME_CHAR:
        // 针对IME输入的优化
        ++g_platform.ime_char_count;
        break;
    case WM_CHAR:
        // 针对IME输入的优化
        if (g_platform.ime_char_count) {
            auto& index = g_platform.ime_char_index;
            const auto buf = g_platform.ime_buf;
            buf[index++] = char16_t(wParam);
            if (index == g_platform.ime_char_count || 
                index == IME_BUF_LEN ||
                (index == IME_BUF_LEN - 1 && is_low_surrogate(char16_t(wParam)) )) {
                g_platform.ime_char_count -= index;
                const U16View view{ buf, buf + index };
                index = 0;
                g_platform.Doc().GuiText(view);
            }
        }
        // 单个输入
        else {
            char32_t ch = static_cast<char32_t>(wParam);
            if (is_low_surrogate(char16_t(wParam))) {
                ch = char16x2_to_char32(g_platform.save_u16, char16_t(wParam));
            }
            else if (is_high_surrogate(char16_t(wParam))) {
                g_platform.save_u16 = char16_t(wParam);
                break;
            }
            // 有效字符:  \b 之类的控制符不算
            if ((ch >= 0x20 && ch != 0x7f) || ch == '\t')
                g_platform.Doc().GuiChar(ch);
        }
        break;
    case WM_KEYDOWN:
        handled = true;
        gui_op = true;
        result = 0;
        ctrl = (::GetKeyState(VK_CONTROL) & 0x80) != 0;
        shift = (::GetKeyState(VK_SHIFT) & 0x80) != 0;
        switch (wParam)
        {
        default:
            handled = false;
            break;
        case 'A':
            // Ctrl + A
            if (ctrl)  gui_op = g_platform.Doc().GuiSelectAll();
            break;
        case 'Z':
            // Ctrl + Z
            if (ctrl)  gui_op = g_platform.Doc().GuiUndo();
            break;
        case 'Y':
            // Ctrl + Y
            if (ctrl)  gui_op = g_platform.Doc().GuiRedo();
            break;
        case 'X':
        case 'C':
            // Ctrl + C
            if (ctrl) {
                dr = g_platform.Doc().GetSelectionRange();
                if (dr.begin.line != dr.end.line || dr.begin.pos != dr.end.pos) {
                    std::wstring str;
                    g_platform.Doc().GenText(&str, dr.begin, dr.end);
                    ::CopyTextToClipboard(str);
                    // 删除选择区
                    if (wParam == 'X') gui_op = g_platform.Doc().GuiDelete(false);
                }
            }
            break;
        case 'V':
            // Ctrl + V
            if (ctrl) {
                std::u16string str;
                ::PasteTextToClipboard(reinterpret_cast<std::wstring&>(str));
                const char16_t* const a = str.c_str();
                const char16_t* const b = a + str.length();
                gui_op = g_platform.Doc().GuiText({ a, b });
            }
            break;
        case VK_RETURN:
            gui_op = g_platform.Doc().GuiReturn();
            break;
        case VK_LEFT:
            gui_op = g_platform.Doc().GuiLeft(ctrl, shift);
            break;
        case VK_UP:
            gui_op = g_platform.Doc().GuiUp(ctrl, shift);
            break;
        case VK_RIGHT:
            gui_op = g_platform.Doc().GuiRight(ctrl, shift);
            break;
        case VK_DOWN:
            gui_op = g_platform.Doc().GuiDown(ctrl, shift);
            break;
        case VK_HOME:
            gui_op = g_platform.Doc().GuiHome(ctrl, shift);
            break;
        case VK_END:
            gui_op = g_platform.Doc().GuiEnd(ctrl, shift);
            break;
        case VK_PRIOR:
            gui_op = g_platform.Doc().GuiPageUp(ctrl, shift);
            break;
        case VK_NEXT:
            gui_op = g_platform.Doc().GuiPageDown(ctrl, shift);
            break;
        case VK_BACK:
            gui_op = g_platform.Doc().GuiBackspace(ctrl);
            break;
        case VK_DELETE:
            gui_op = g_platform.Doc().GuiDelete(ctrl);
            break;
        case VK_F1:
            gui_op = g_platform.Doc().GuiFontSize(20.f);
            break;
        case VK_F2:
            gui_op = g_platform.Doc().GuiFontName(2);
            break;
        case VK_F3:
            gui_op = g_platform.Doc().GuiUnerline(CEDTextDocument::Set_Change);
            break;
        case VK_F4:
            gui_op = g_platform.Doc().GuiItalic(CEDTextDocument::Set_Change);
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
        case VK_F7:
            gui_op = g_platform.Doc().GuiInline(*info.as_info(), info.create_img(L"../common/2.png"), Type_Image);
            break;
        case VK_F8:
            gui_op = g_platform.Doc().GuiRuby(U'漢', u"かん"_red);
            break;
        case VK_F9:
            dr = g_platform.Doc().GetSelectionRange();
            if (dr.begin.line != dr.end.line || dr.begin.pos != dr.end.pos) {
                msg_box_text(dr.begin, dr.end);
            }
            break;
        case VK_F11:
            msg_box_text({}, { g_platform.Doc().GetLogicLineCount() });
            break;
        }
        if (!gui_op) ::MessageBeep(MB_ICONWARNING);
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        handled = true;
        result = 1;
        break;
        // IME 支持
    //case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        //handled = true;
        g_platform.SetImePosition();
        break;
    }

    if (!handled) result = ::DefWindowProcW(hwnd, message, wParam, lParam);
    return result;
}


/// <summary>
/// Sets the IME position.
/// </summary>
/// <returns></returns>
void WinDWnD2D::SetImePosition() noexcept {
    auto caret = this->Doc().GetCaret();
#ifndef NDEBUG
    char buf[256]; buf[0] = 0;
    std::snprintf(
        buf, sizeof(buf) / sizeof(buf[0]), 
        "caret[%f, %f]",
        caret.x, caret.y
    );
    //this->DebugOutput(buf);
#endif // NDEBUG
    // 判断有效性
    if (caret.x >= 0.0 && caret.y >= 0.0 && caret.x < ctrl_w && caret.y < ctrl_h) {
        BOOL ret = FALSE;
        HIMC imc = ::ImmGetContext(this->data.hwnd);
        // 0.8 保持一定美观
        const float RA = 0.8f;
        caret.y += caret.height * (1.f - RA) * 0.5f;
        caret.height *= RA;

        if (::ImmGetOpenStatus(imc)) {
            COMPOSITIONFORM cf = { 0 };
            cf.dwStyle = CFS_POINT;
            cf.ptCurrentPos.x = static_cast<LONG>(CTRL_X + caret.x);
            cf.ptCurrentPos.y = static_cast<LONG>(CTRL_Y + caret.y);
            if (ImmSetCompositionWindow(imc, &cf)) {
                LOGFONTW lf = { 0 };
                lf.lfHeight = static_cast<LONG>(caret.height);
                //lf.lfItalic
                if (ImmSetCompositionFontW(imc, &lf)) {
                    ret = TRUE;
                }
            }
        }
        ::ImmReleaseContext(this->data.hwnd, imc);
    }
}


/// <summary>
/// Recreates the context.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="prev">The previous.</param>
/// <returns></returns>
void WinDWnD2D::RecreateContext(CEDTextCell& cell) noexcept {
    // 图片
    if (cell.RefMetaInfo().metatype == Type_Image)
        return this->RecreateContextImage(cell);
    // 释放之前的布局
    const auto prev_layout = reinterpret_cast<IDWriteTextLayout*>(cell.ctx.context);
    cell.ctx.context = nullptr;
    if (prev_layout) prev_layout->Release();
    // 利用字符串创建DWrite文本布局
    const auto& str = cell.RefString();
    // 空的场合
    if (!str.length) {
        if (cell.RefMetaInfo().dirty) {
            cell.metrics.width = 0;
            const auto h = cell.RefRichED().size;
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
        if (this->Doc().RefInfo().flags & Flag_RichText) {
            const auto& riched = cell.RefRichED();
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
    HRESULT hr = S_OK; 
    const auto cell_context = reinterpret_cast<IDWriteTextLayout**>(&cell.ctx.context);
    this->Doc().PWHelperView([=, &hr](RichED::U16View view) noexcept {
        static_assert(sizeof(wchar_t) == sizeof(view.first[0]), "must be same!");
        hr = this->data.dw1_factory->CreateTextLayout(
            reinterpret_cast<const wchar_t*>(view.first), view.second- view.first,
            fmt, 0, 0, cell_context
        );
    }, cell);
    //static_assert(sizeof(wchar_t) == sizeof(str.data[0]), "must be same!");
    //auto hr = this->data.dw1_factory->CreateTextLayout(
    //    reinterpret_cast<const wchar_t*>(str.data), str.length,
    //    fmt, 0, 0, reinterpret_cast<IDWriteTextLayout**>(&cell.ctx.context)
    //);
    // 测量CELL
    if (cell.RefMetaInfo().dirty) {
        const auto layout = reinterpret_cast<IDWriteTextLayout*>(cell.ctx.context);
        const bool ver = this->Doc().RefMatrix().read_direction & 1;
        DWRITE_TEXT_METRICS dwtm;
        DWRITE_LINE_METRICS dwlm;
        DWRITE_OVERHANG_METRICS dwom;
        if (SUCCEEDED(hr)) {
            if (ver) {
                layout->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
                layout->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
            }
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
            // 垂直
            if (ver) {
                cell.metrics.width = dwtm.height;
                this->Doc().VAlignHelperH(dwlm.baseline, dwlm.height, cell.metrics);
                cell.metrics.bounding.left = -dwom.top;
                cell.metrics.bounding.top = -dwom.right;
                cell.metrics.bounding.right = dwom.bottom;
                cell.metrics.bounding.bottom = dwom.left;
            }
            // 水平
            else {
                cell.metrics.width = dwtm.widthIncludingTrailingWhitespace;
                this->Doc().VAlignHelperH(dwlm.baseline, dwlm.height, cell.metrics);
                cell.metrics.bounding.left = -dwom.left;
                cell.metrics.bounding.top = -dwom.top;
                cell.metrics.bounding.right = dwom.right;
                cell.metrics.bounding.bottom = dwom.bottom;
            }

        }
        // CLEAN!
        cell.AsClean();
    }
    // 释放
    fmt->Release();
}

void WinDWnD2D::RecreateContextImage(CEDTextCell & cell) noexcept {
    // 释放之前的图片
    auto& prev_bitmap  = reinterpret_cast<ID2D1Bitmap*&>(cell.ctx.context);
    if (prev_bitmap) {
        assert(!"???");
        prev_bitmap->Release();
    }
    prev_bitmap = nullptr;
    const auto info = cell.GetExtraInfo();
    const auto extra = reinterpret_cast<DWnD2DImageExtraInfo*>(info);
    const auto code = LoadBitmapFromFile(
        this->data.d2d_rendertarget,
        this->data.wic_factory,
        extra->uri,
        &prev_bitmap
    );
    if (cell.RefMetaInfo().dirty) {
        float width = 32.f, height = 32.f;
        if (prev_bitmap) {
            const auto s = prev_bitmap->GetSize();
            width = s.width;
            height = s.height;
        }
        const bool ver = this->Doc().RefMatrix().read_direction & 1;
        const float ratio = ver ? 0.5f : 0.9f;
        this->Doc().VAlignHelperH(height*ratio, height, cell.metrics);
        cell.metrics.width = width;
        cell.metrics.bounding.left = 0;
        cell.metrics.bounding.top = 0;
        cell.metrics.bounding.right = width;
        cell.metrics.bounding.bottom = height;
        cell.AsClean();
    }
}

/// <summary>
/// Deletes the context.
/// </summary>
/// <param name="cell">The cell.</param>
/// <returns></returns>
void WinDWnD2D::DeleteContext(CEDTextCell& cell) noexcept {
    //if (!cell.ctx.context) return;
    auto& ptr = reinterpret_cast<IUnknown*&>(cell.ctx.context);
    ::SafeRelease(ptr);
}

/// <summary>
/// Draws the context.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="baseline">The baseline.</param>
/// <returns></returns>
void WinDWnD2D::DrawContext(CtxPtr ctx, CEDTextCell& cell, unit_t baseline) noexcept {
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    // 图片
    if (cell.RefMetaInfo().metatype == Type_Image)
        return DrawContextImage(cell, baseline);
    D2D1_POINT_2F point;
    point.x = cell.metrics.pos + cell.metrics.offset.x;
    // 失败则不渲染
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        const auto brush = this->data.d2d_brush;
        // 富文本
        if (this->Doc().RefInfo().flags & Flag_RichText) 
            brush->SetColor(D2D1::ColorF(cell.RefRichED().color));
        else 
            brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
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
        this->Map(&cell_rect.left, &cell_rect.right);
        renderer->FillRectangle(&cell_rect, dcb);
#endif
        this->Map(&point.x);
        renderer->DrawTextLayout(point, ptr, brush);
    }
    // 下划线
    if (cell.RefRichED().effect & Effect_Underline) {
        point.y = baseline + cell.metrics.dr_height + cell.metrics.offset.y;
        const auto renderer = this->data.d2d_rendertarget;
        auto point2 = point; point2.x += cell.metrics.width;
        const auto brush = this->data.d2d_brush;
        this->Map(&point.x, &point2.x);
        renderer->DrawLine(point, point2, brush);
    }
}


/// <summary>
/// Draws the context image.
/// </summary>
/// <param name="">The .</param>
/// <param name="y">The y.</param>
/// <returns></returns>
void WinDWnD2D::DrawContextImage(CEDTextCell & cell, unit_t baseline) noexcept {
    if (const auto ptr = static_cast<ID2D1Bitmap*>(cell.ctx.context)) {
        D2D1_RECT_F rect;
        rect.left = cell.metrics.pos + cell.metrics.offset.x;
        rect.top = baseline - cell.metrics.ar_height + cell.metrics.offset.y;
        rect.right = cell.metrics.width + rect.left;
        rect.bottom = cell.metrics.ar_height + cell.metrics.dr_height + rect.top;
        const auto renderer = this->data.d2d_rendertarget;
        this->Map(&rect.left, &rect.right);
        renderer->DrawBitmap(ptr, &rect);
    }
}


/// <summary>
/// Hits the test image.
/// </summary>
/// <param name="">The .</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::HitTestObject(CEDTextCell & cell, unit_t offset) noexcept -> CellHitTest {
    CellHitTest rv = { 0, 0, 1 };
    if (offset >= cell.metrics.width * 0.5)
        rv.pos = 1;
    return rv;
}

/// <summary>
/// Hits the test.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::HitTest(CEDTextCell& cell, unit_t offset) noexcept->CellHitTest {
    if (cell.RefMetaInfo().metatype >= Type_InlineObject)
        return HitTestObject(cell, offset);
    CellHitTest rv = { 0 };
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    // 失败则不获取
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        //offset -= cell.metrics.offset.x;
        BOOL hint = false, inside = false;
        DWRITE_HIT_TEST_METRICS htm;
        ptr->HitTestPoint(offset, offset, &hint, &inside, &htm);
        rv.pos = htm.textPosition;
        rv.trailing = hint;
        rv.length = htm.length;
        this->Doc().PWHelperHit(cell, rv);
    }
    return rv;
}

/// <summary>
/// Gets the character metrics image.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::GetCharMetricsImage(CEDTextCell& cell, uint32_t pos) noexcept -> CharMetrics {
    CharMetrics cm;
    if (pos) {
        cm.offset = cell.metrics.width;
        cm.width = 0;
    }
    else {
        cm.offset = 0;
        cm.width = cell.metrics.width;
    }
    return cm;
}

/// <summary>
/// Gets the character metrics.
/// </summary>
/// <param name="cell">The cell.</param>
/// <param name="offset">The offset.</param>
/// <returns></returns>
auto WinDWnD2D::GetCharMetrics(CEDTextCell& cell, uint32_t pos) noexcept -> CharMetrics {
    if (cell.RefMetaInfo().metatype == Type_Image)
        return GetCharMetricsImage(cell, pos);
    CharMetrics cm = { 0 };
    // 睡眠状态则唤醒
    if (!cell.ctx.context) this->RecreateContext(cell);
    // 失败则不获取
    if (const auto ptr = static_cast<IDWriteTextLayout*>(cell.ctx.context)) {
        assert(pos <= cell.RefString().length);
        if (pos == cell.RefString().length) {
            cm.offset = cell.metrics.width;
        }
        else try {
            // 密码模式
            const auto real_pos = this->Doc().PWHelperPos(cell, pos);
            std::vector<DWRITE_CLUSTER_METRICS> buf;
            const uint32_t size = real_pos + 1;
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
                if (char_index >= real_pos) break;
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
void WinDWnD2D::DebugOutput(const char* text, bool high) noexcept {
    std::printf("[RichED %s] %s\n", high ? "Error" : " Hint", text);
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


void WinDWnD2D::DrawSelection() noexcept {
    const auto brush = this->data.d2d_brush;
    brush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.5f));
    const auto renderer = this->data.d2d_rendertarget;
    const auto& vec = this->Doc().RefSelection();
    for (auto& rect : vec) {
        D2D1_RECT_F rc;
        rc.left = rect.left;
        rc.top = rect.top;
        rc.right = rect.right;
        rc.bottom = rect.bottom;
        
        this->Map(&rc.left, &rc.right);
        renderer->FillRectangle(rc, brush);
    }
}

void WinDWnD2D::Update() noexcept {
    const auto flag = this->Doc().Update();
    // View
    if (flag & Changed_View) {
        ::InvalidateRect(this->data.hwnd, nullptr, false);
    }
    // Caret
    if (flag & Changed_Caret) {
        ::SetTimer(data.hwnd, reinterpret_cast<uintptr_t>(this), caret_blink_time, nullptr);
        this->caret_blink_i = 1;
        ::InvalidateRect(this->data.hwnd, nullptr, false);
    }
    // Text
    if (flag & Changed_Text) {
        //std::wstring str;
        //g_platform.Doc().GenText(&str, {}, { g_platform.Doc().GetLogicLineCount() });
        //str.append(L"\r\n");
        //::OutputDebugStringW(str.c_str());
    }
}

/// <summary>
/// Renders this instance.
/// </summary>
/// <returns></returns>
void WinDWnD2D::Render() noexcept {
    //this->Doc().BeforeRender();
    this->cell_draw_i = 0;

    const auto renderer = this->data.d2d_rendertarget;
    if (renderer->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) return;
    renderer->BeginDraw();
    renderer->Clear(D2D1::ColorF(0x66ccff));
    renderer->SetTransform(D2D1::Matrix3x2F::Translation(CTRL_X, CTRL_Y));
    renderer->DrawRectangle({0, 0, ctrl_w, ctrl_h }, this->data.d2d_border);

    this->DrawSelection();
    this->Doc().Render(nullptr);
    if (this->caret_blink_i) {
        const auto caret = this->Doc().GetCaret();
        // GetCaret返回的矩形宽度没有意义, 可以进行自定义
        const float custom_width = 2.f;

        D2D1_RECT_F rect = {
            caret.x - custom_width * 0.5f,      // 向前后偏移一半
            caret.y + 1,                        // 上下保留一个单位的空白以保持美观
            caret.x + custom_width * 0.5f,      // 向后后偏移一半
            caret.y + caret.height - 1          // 上下保留一个单位的空白以保持美观
        };

        this->Map(&rect.left, &rect.right);

        renderer->FillRectangle(rect, this->data.d2d_black);
    }
    const auto hr = renderer->EndDraw();

    if (FAILED(hr)) std::exit(ECODE_DRAW);
}



auto LoadBitmapFromFile(
    ID2D1RenderTarget* pRenderTarget,
    IWICImagingFactory* pIWICFactory,
    PCWSTR uri,
    ID2D1Bitmap** ppBitmap
) noexcept -> HRESULT {
    IWICBitmapDecoder *pDecoder = nullptr;
    IWICBitmapFrameDecode *pSource = nullptr;
    IWICStream *pStream = nullptr;
    IWICFormatConverter *pConverter = nullptr;
    IWICBitmapScaler *pScaler = nullptr;

    HRESULT hr = pIWICFactory->CreateDecoderFromFilename(
        uri,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &pDecoder
    );

    if (SUCCEEDED(hr)) {
        hr = pDecoder->GetFrame(0, &pSource);
    }
    if (SUCCEEDED(hr)) {
        hr = pIWICFactory->CreateFormatConverter(&pConverter);
    }


    if (SUCCEEDED(hr)) {
        hr = pConverter->Initialize(
            pSource,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.f,
            WICBitmapPaletteTypeMedianCut
        );
    }
    if (SUCCEEDED(hr)) {
        hr = pRenderTarget->CreateBitmapFromWicBitmap(
            pConverter,
            nullptr,
            ppBitmap
        );
    }
    ::SafeRelease(pDecoder);
    ::SafeRelease(pSource);
    ::SafeRelease(pStream);
    ::SafeRelease(pConverter);
    ::SafeRelease(pScaler);
    return hr;
}