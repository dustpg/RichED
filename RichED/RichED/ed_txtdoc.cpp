#include "ed_txtdoc.h"
#include "ed_txtplat.h"
#include "ed_txtcell.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>

enum { RED_INIT_ARRAY_BUFLEN = 32 };
enum { RED_DIRTY_ARRAY_SIZE = 256 };

// CJK LUT
RED_LUT_ALIGNED const uint32_t RED_CJK_LUT[] = {
    0x00000000, 0xfffc0000, 0xffffffff, 0xffffffff, 
    0xffffffff, 0x00000000, 0x00000000, 0x06000000, 
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 
    0xffffffff, 0xffffffff, 0xffffffff, 0x07000fff, 
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

// namesapce RichED::detail
namespace RichED { namespace detail {
    // is_surrogate
    inline bool is_surrogate(uint16_t ch) noexcept { return ((ch) & 0xF800) == 0xD800; }
    // is_low_surrogate
    inline bool is_low_surrogate(uint16_t ch) noexcept { return ((ch) & 0xFC00) == 0xDC00; }
    // is_high_surrogate
    inline bool is_high_surrogate(uint16_t ch) noexcept { return ((ch) & 0xFC00) == 0xD800; }
    // char16 x2 -> char32
    inline char32_t char16x2to32(char16_t lead, char16_t trail) {
        assert(is_high_surrogate(lead) && "illegal utf-16 char");
        assert(is_low_surrogate(trail) && "illegal utf-16 char");
        return char32_t((uint16_t(lead) - 0xD800) << 10 | (uint16_t(trail) - 0xDC00)) + (0x10000);
    };
    // count char32_t 
    static uint32_t count(U16View view) noexcept {
        uint32_t c = 0;
        while (view.first < view.second) {
            if (is_low_surrogate(*view.first)) {
                view.first++;
                assert(is_high_surrogate(*view.first));
            }
            c++;
            view.first++;
        }
        return 0;
    }
    // lf count 
    static uint32_t lfcount(U16View view) noexcept {
        uint32_t c = 0;
        while (view.first < view.second) {
            if (*view.first == '\n') c++;
            view.first++;
        }
        return c;
    }
    // lf view
    static U16View lfview(U16View& view) noexcept {
        // 支持\r\n, \n. 暂不支持\r
        U16View rv = view;
        uint32_t count = 0;
        while (view.first < view.second) {
            const auto ch = *view.first;
            view.first++;
            if (ch == '\n')  {
                rv.second = view.first - 1;
                if (count && rv.second[-1] == '\r') --rv.second;
                break;
            }
            ++count;
        }
        return rv;
    }
    // nice view 1
    static U16View nice_view1(U16View& view, uint32_t len) noexcept {
        U16View rv;
        auto itr = rv.first = view.first; 
        const uint32_t real_len = std::min(len, uint32_t(view.second - view.first));

        for (uint32_t i = 0; i != real_len; ++i) {
            if (*itr == '\n') {
                if (i && itr[-1] == '\r') --itr;
                break;
            }
            ++itr;
        }
        if (real_len && detail::is_low_surrogate(itr[-1])) ++itr;
        rv.second = view.first = itr;
        return rv;
    }
    // nice view 2
    static U16View nice_view2(U16View& view, uint32_t len) noexcept {
        U16View rv; 
        auto itr = rv.second = view.second;
        const uint32_t real_len = std::min(len, uint32_t(view.second - view.first));
        for (uint32_t i = 0; i != real_len; ++i) {
            if (itr[-1] == '\n') break;
            --itr;
        }
        if (real_len && detail::is_high_surrogate(itr[-1])) --itr;
        rv.first = view.second = itr;
        return rv;
    }
    /// <summary>
    /// Resizes the buffer.
    /// </summary>
    /// <param name="buf">The buf.</param>
    /// <param name="plat">The plat.</param>
    /// <param name="clean">The clean.</param>
    /// <param name="len">The length.</param>
    /// <param name="">The .</param>
    /// <returns></returns>
    template<typename T>
    bool resize_buffer(CEDBuffer<T>& buf, IEDTextPlatform& plat, uint32_t len) noexcept {
        for (uint32_t i = 0; ; ++i) {
            if (buf.Resize(len)) return true;
            if (plat.OnOOM(i) == OOM_Ignore) return false;
        }
    }
    // is cjk
    static uint32_t is_cjk(uint32_t ch) noexcept {
        // 假定区域以0x100对齐
        const uint32_t ch2 = (ch & 0x3ffff) >> 8;
        const uint32_t index = ch2 >> 5;
        const uint32_t mask = 1 << (ch2 & 0x1f);
        return RED_CJK_LUT[index] & mask;
    }
    // utf-32 to utf-16
    inline uint16_t utf32to16(char32_t ch, char16_t buffer[2]) {
        // utf32 -> utf32x2
        if (ch > 0xFFFF) {
            // From http://unicode.org/faq/utf_bom.html#35
            buffer[0] = static_cast<char16_t>(0xD800 + (ch >> 10) - (0x10000 >> 10));
            buffer[1] = static_cast<char16_t>(0xDC00 + (ch & 0x3FF));
            return 2;
        }
        buffer[0] = static_cast<char16_t>(ch);
        return 1;
    }
    // cell from node
    inline auto next_cell(Node* node) noexcept {
        return static_cast<CEDTextCell*>(node->next); }
    // cell from node
    inline auto next_cell(const Node* node) noexcept {
        return static_cast<const CEDTextCell*>(node->next); }
    // itr
    template<typename T> struct nitr {
        // cell node
        T*      cell;
        // operator *
        auto&operator*() noexcept { return *cell; }
        // operator !=
        bool operator!=(nitr node) const noexcept { return cell != node.cell; }
        // operator ++
        nitr&operator++() noexcept { cell = next_cell(cell); return *this; }
        // operator ++(int)
        nitr operator++(int) noexcept { nitr itr = *this; ++(*this); return itr; }
    };
    // for helper
    template<typename T> struct for_cells {
        // begin node
        T*          begin_cell;
        // end node
        T*          end_cell;
        // begin
        auto begin() const noexcept { return nitr<T>{ static_cast<T*>(begin_cell) }; }
        // end
        auto end() const noexcept { return nitr<T>{ static_cast<T*>(end_cell) }; }
    };
    // get for_cells
    inline auto cfor_cells(Node* a, Node* b) noexcept {
        return for_cells<CEDTextCell>{ static_cast<CEDTextCell*>(a),  static_cast<CEDTextCell*>(b) };
    }
    // get for_cells
    inline auto cfor_cells(const Node* a, const Node* b) noexcept {
        return for_cells<const CEDTextCell>{ static_cast<const CEDTextCell*>(a), static_cast<const CEDTextCell*>(b) };
    }
    // push line data
    static bool push_data(CEDBuffer<VisualLine>& vlv, const VisualLine& line) noexcept {
        const auto size = vlv.GetSize();
        if (vlv.IsFull()) {
            if (!vlv.Resize(size + (size >> 1))) return false;
        }
        vlv.Resize(size + 1);
        vlv[size] = line;
        return true;
    }
    // txtoff
    struct txtoff_t { CEDTextCell* cell; uint32_t pos; };
    // find
    PCN_NOINLINE static auto find_cell1_txtoff(CEDTextCell* cell, uint32_t pos) noexcept {
        // 遍历到合适的位置
        while (pos > cell->GetString().length) {
            pos -= cell->GetString().length;
            cell = detail::next_cell(cell);
        }
        const txtoff_t rv = { cell, pos };
        return rv;
    }
    // find
    PCN_NOINLINE static auto find_cell2_txtoff(CEDTextCell* cell, uint32_t pos) noexcept {
        // 遍历到合适的位置
        while (pos >= cell->GetString().length) {
            pos -= cell->GetString().length;
            cell = detail::next_cell(cell);
        }
        const txtoff_t rv = { cell, pos };
        return rv;
    }
    // find
    static void find_cell1_txtoff_ex(CEDTextCell*& cell, uint32_t& pos) noexcept {
        const auto val = find_cell1_txtoff(cell, pos);
        cell = val.cell; pos = val.pos;
    }
    // find
    static void find_cell2_txtoff_ex(CEDTextCell*& cell, uint32_t& pos) noexcept {
        const auto val = find_cell2_txtoff(cell, pos);
        cell = val.cell; pos = val.pos;
    }
}}



// namespace RichED::impl 
namespace RichED { namespace impl {
    // selection mode
    enum selection_mode : uint32_t {
        mode_all = 0,       // 全选文本
        mode_target,        // 鼠标点击
        mode_logicup,       // 上升一行
        mode_logicdown,     // 下降一行
        mode_logicleft,     // 左移一下
        mode_logicright,    // 右移一下
        //mode_home,          // 一行起始
        //mode_end,           // 一行结束
        //mode_first,         // 文本开头
        //mode_last,          // 文本结尾
        //mode_leftchar,      // 左移字符
        //mode_rightchar,     // 右移字符
        //mode_leftword,      // 左移字段
        //mode_rightword,     // 右移字段
    };
}}


// this namesapce
namespace RichED {
    // check range
    struct CheckRangeCtx {
        CellPoint   begin;
        CellPoint   end;
        LogicLine   line2;
        LogicLine*  line1;
    };
    // hittest
    struct HitTestCtx {
        // visual line
        const VisualLine*   visual_line;
        // text cell
        CEDTextCell*        text_cell;
        // pos: before cell
        uint32_t            len_before_cell;
        // pos: in cell
        uint32_t            len_in_cell;
    };
    // priavate impl for CEDTextDocument
    struct CEDTextDocument::Private {
        // hit doc from position
        static bool HitTest(CEDTextDocument& doc, Point, HitTestCtx&) noexcept;
        // check range
        static bool CheckRange(CEDTextDocument& doc, DocPoint begin, DocPoint end, CheckRangeCtx& ctx) noexcept;
        // check wrap mode
        static auto CheckWrap(CEDTextDocument& doc, CEDTextCell& cell, unit_t pos) noexcept->CEDTextCell*;
        // expand visual line clean area
        static void ExpandVL(CEDTextDocument& doc, Node& end, unit_t) noexcept;
        // recreate cell
        static void Recreate(CEDTextDocument&doc, CEDTextCell& cell) noexcept;
        // set selection
        static void SetSelection(CEDTextDocument& doc, DocPoint dp, uint32_t mode, bool ) noexcept;
        // insert text
        static bool Insert(CEDTextDocument& doc, DocPoint dp, U16View, LogicLine, bool behind)noexcept;
        // insert cell
        static bool Insert(CEDTextDocument& doc, DocPoint dp, CEDTextCell&, LogicLine&)noexcept;
        // remove text
        static bool Remove(CEDTextDocument& doc, DocPoint begin, DocPoint end, const CheckRangeCtx&)noexcept;
        // dirty
        static void Dirty(CEDTextDocument& doc, CEDTextCell& cell, uint32_t logic_line)noexcept;
        // merge 
        static bool Merge(CEDTextDocument& doc, CEDTextCell& cell, unit_t, unit_t) noexcept;
    };
    // RichData ==
    inline bool operator==(const RichData& a, const RichData& b) noexcept {
        return !std::memcmp(&a, &b, sizeof(a));
    }
    // redraw
    inline void NeedRedraw(CEDTextDocument& doc) noexcept {
        doc.platform.ValueChanged(IEDTextPlatform::Changed_View);
    }
    // cmp
    inline auto Cmp(DocPoint dp) noexcept {
        uint64_t u64;
        u64 = (uint64_t(dp.line) << 32) | uint64_t(dp.pos);
        return u64;
    }
#ifndef NDEBUG
    /// <summary>
    /// Gets the length of the line text.
    /// </summary>
    /// <param name="cell">The cell.</param>
    /// <returns></returns>
    static uint32_t GetLineTextLength(CEDTextCell* cell) noexcept {
        uint32_t length = 0;
        while (true) {
            length += cell->GetString().length;
            if (cell->GetMetaInfo().eol) break;
            cell = detail::next_cell(cell);
        }
        return length;
    }
#endif
}


/// <summary>
/// Initializes a new instance of the <see cref="CEDTextDocument" /> class.
/// </summary>
/// <param name="plat">The plat.</param>
/// <param name="arg">The argument.</param>
RichED::CEDTextDocument::CEDTextDocument(IEDTextPlatform& plat, const DocInitArg& arg) noexcept
: platform(plat) {
    // 初始化
    this->default_riched = arg.riched;
    // 默认使用CRLF
    m_linefeed.AsCRLF();
    // 初始化相关数据
    m_rcViewport = { 0, 0, 0, 0 };
    m_rcCaret = { 0, 0, 1, arg.riched.size };
    m_szEstimated = { 0, 0 };
    m_dpAnchor = { 0, 0 };
    m_dpCaret = { 0, 0 };
    // 处理Cell节点
    m_head.prev = nullptr;
    m_head.next = &m_tail;
    m_tail.prev = &m_head;
    m_tail.next = nullptr;
    // 初始化INFO
    std::memset(&m_info, 0, sizeof(m_info));
    m_info.flags            = arg.flags;
    m_info.password         = arg.password;
    m_info.fixed_lineheight = arg.fixed_lineheight;
    m_info.valign           = arg.valign;
    //m_info.talign           = arg.talign;
    m_info.wrap_mode        = arg.wrap_mode;
#ifndef NDEBUG
    // 防止越界用调试缓存
    std::memset(m_dbgBuffer, 233, sizeof(m_dbgBuffer));
#endif
    // 初始CELL
    const auto cell = RichED::CreateNormalCell(*this, arg.riched);
    // 缓存
    m_vLogic.Resize(RED_INIT_ARRAY_BUFLEN);
    m_vVisual.Resize(RED_INIT_ARRAY_BUFLEN);
    if (m_vVisual.IsFailed() | m_vLogic.IsFailed() | !cell) {
        arg.code = DocInitArg::CODE_OOM;
        return;
    }
    // 第一行数据
    cell->AsEOL();
    RichED::InsertAfterFirst(m_head, *cell);
    m_vLogic.Resize(1);
    m_vLogic[0] = { cell, 0 };
    m_vVisual.Resize(1);
    //m_vVisual[0] = { static_cast<CEDTextCell*>(&m_head), uint32_t(-1) };
    m_vVisual[0] = { cell, 0 };
}

/// <summary>
/// Finalizes an instance of the <see cref="CEDTextDocument"/> class.
/// </summary>
/// <returns></returns>
RichED::CEDTextDocument::~CEDTextDocument() noexcept {
    // 释放CELL链
    auto cell = detail::next_cell(&m_head);
    while (cell != &m_tail) {
        const auto node = cell;
        cell = detail::next_cell(cell);
        node->Dispose();
    }
    m_head.next = &m_tail;
    m_tail.prev = &m_head;
}

/// <summary>
/// Updates this instance.
/// </summary>
/// <returns></returns>
void RichED::CEDTextDocument::BeforeRender() noexcept {
    const auto bottom = m_rcViewport.y + m_rcViewport.height;
    const auto maxbtm = max_unit();
    Private::ExpandVL(*this, m_tail, maxbtm);
}

/// <summary>
/// Renders this instance.
/// </summary>
/// <returns></returns>
void RichED::CEDTextDocument::Render() noexcept {
    if (m_vVisual.GetSize()) {
        const auto count = m_vVisual.GetSize();
        const auto data = m_vVisual.GetData();
        const auto end_line = data + count - 1;
        auto this_line = data;
        // [0, count - 1)
        while (this_line != end_line) {
            const auto next_line = this_line + 1;
            const auto cells = detail::cfor_cells(this_line->first, next_line->first);
            const auto baseline = this_line->offset + this_line->ar_height_max;
            // TODO: 固定行高
            for (auto& cell : cells) {
                this->platform.DrawContext(cell, baseline);
            }
            this_line = next_line;
        }
    }
    //const auto cells = detail::cfor_cells(m_head.next, &m_tail);
    //for (auto& cell : cells) {
    //    this->platform.DrawContext(const_cast<CEDTextCell&>(cell));
    //}
}


/// <summary>
/// Sets the position.
/// </summary>
/// <param name="pos">The position.</param>
/// <returns></returns>
void RichED::CEDTextDocument::SetPos(Point pos) noexcept {
    m_rcViewport.x = pos.x;
    m_rcViewport.y = pos.y;
}


/// <summary>
/// Inserts the text.
/// </summary>
/// <param name="dp">The dp.</param>
/// <param name="view">The view.</param>
/// <param name="riched">The riched.</param>
/// <param name="behind">if set to <c>true</c> [behind].</param>
/// <returns></returns>
bool RichED::CEDTextDocument::InsertText(DocPoint dp, U16View view, bool behind) noexcept {
    if (dp.line < m_vLogic.GetSize()) {
        const auto line_data = m_vLogic[dp.line];
        dp.pos = std::min(dp.pos, line_data.length);
        return Private::Insert(*this, dp, view, line_data, behind);
    }
    assert(!"OUT OF RANGE");
    return false;
}


/// <summary>
/// Inserts the ruby.
/// </summary>
/// <param name="dp">The dp.</param>
/// <param name="ch">The ch.</param>
/// <param name="view">The view.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::InsertRuby(DocPoint dp, char32_t ch, U16View view, const RichData* rich_ptr) noexcept {
    assert(ch && "bad char");
    if (dp.line < m_vLogic.GetSize()) {
        // 不能包含换行信息
        const auto real_view = detail::lfview(view);
        if (real_view.first == real_view.second) return assert(!"empty"), false;
        auto& line_data = m_vLogic[dp.line];
        dp.pos = std::min(dp.pos, line_data.length);
        // 创建一个简单的
        auto riched = rich_ptr ? *rich_ptr : default_riched; 
        riched.size = half(riched.size);
        const auto cell = RichED::CreateShrinkedCell(*this, riched);;
        if (!cell) return false;
        const_cast<CellMeta&>(cell->GetMetaInfo()).metatype = Type_UnderRuby;
        auto& str = const_cast<FixedStringA&>(cell->GetString());
        str.capacity = str.length = detail::utf32to16(ch, str.data);
        // 插入内联对象
        if (!this->InsertInline(dp, std::move(*cell))) return false;
        // 插入普通数据
        dp.pos += cell->GetString().length;
        const auto rv = Private::Insert(*this, dp, real_view, line_data, false);
        cell->SetRichED(default_riched);
        return rv;
    }
    assert(!"OUT OF RANGE");
    return false;
}

/// <summary>
/// Inserts the inline.
/// </summary>
/// <param name="dp">The dp.</param>
/// <param name="cell">The cell.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::InsertInline(DocPoint dp, CEDTextCell&& cell) noexcept {
    const auto rv = [this, dp, &cell]() mutable noexcept {
        if (dp.line < m_vLogic.GetSize()) {
            auto& line_data = m_vLogic[dp.line];
            dp.pos = std::min(dp.pos, line_data.length);
            return Private::Insert(*this, dp, cell, line_data);
        }
        return false;
    }();
    // 失败则释放对象
    if (!rv) cell.Dispose();
    return rv;
}

/// <summary>
/// Removes the text.
/// </summary>
/// <param name="begin">The begin.</param>
/// <param name="end">The end.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::RemoveText(DocPoint begin, DocPoint end) noexcept {
    CheckRangeCtx ctx;
    if (!Private::CheckRange(*this, begin, end, ctx)) return false;
    return Private::Remove(*this, begin, end, ctx);
}

/// <summary>
/// Resizes the specified size.
/// </summary>
/// <param name="size">The size.</param>
/// <returns></returns>
void RichED::CEDTextDocument::Resize(Size size) noexcept {
    m_rcViewport.width = size.width;
    m_rcViewport.height = size.height;
    // 清除视觉行
    if (m_vVisual.IsOK()) m_vVisual.Resize(1);
    // 重绘
    RichED::NeedRedraw(*this);
}


/// <summary>
/// Sets the line feed.
/// </summary>
/// <param name="lf">The lf.</param>
/// <returns></returns>
void RichED::CEDTextDocument::SetLineFeed(const LineFeed lf) noexcept {
    const auto prev_len = m_linefeed.length;
    m_linefeed = lf;
    // 文本修改
    this->platform.ValueChanged(IEDTextPlatform::Changed_Text);
    // 文本长度修改
    if (prev_len != lf.length) {
        // TODO:
        assert(!"NOT IMPL");
    }
}

/// <summary>
/// valign helper
/// </summary>
/// <param name="ar">The ar.</param>
/// <param name="height">The height.</param>
/// <param name="m">The m.</param>
/// <returns></returns>
void RichED::CEDTextDocument::VAlignHelperH(unit_t ar, unit_t height, CellMetrics& m) noexcept {
    switch (m_info.valign & 3)
    {
    case VAlign_Baseline:
        m.ar_height = ar;
        m.dr_height = height - ar;
        break;
    case VAlign_Ascender:
        m.ar_height = 0;
        m.dr_height = height;
        break;
    case VAlign_Middle:
        m.dr_height = m.ar_height = half(height);
        break;
    case VAlign_Descender:
        m.ar_height = height;
        m.dr_height = 0;
        break;
    }
}

// ----------------------------------------------------------------------------
//                                GUI Operation
// ----------------------------------------------------------------------------

/// <summary>
/// GUIs the l button up.
/// </summary>
/// <param name="pt">The pt.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiLButtonUp(Point pt) noexcept {
    HitTestCtx ctx;
    if (Private::HitTest(*this, pt, ctx)) {
        const DocPoint dp{
            ctx.visual_line->lineno, 
            ctx.len_before_cell + ctx.len_in_cell
        };
        Private::SetSelection(*this, dp, impl::mode_target, false);
        return true;
    }
    return false;
}

/// <summary>
/// GUIs the l button down.
/// </summary>
/// <param name="pt">The pt.</param>
/// <param name="shift">if set to <c>true</c> [shift].</param>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiLButtonDown(Point pt, bool shift) noexcept {
    return true;
}

/// <summary>
/// GUIs the l button hold.
/// </summary>
/// <param name="pt">The pt.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiLButtonHold(Point pt) noexcept {
    return true;
}

/// <summary>
/// GUI: the character.
/// </summary>
/// <param name="ch">The ch.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiChar(char32_t ch) noexcept {
    char16_t buf[2];
    const auto len = detail::utf32to16(ch, buf);
    return this->GuiText({ buf, buf + len });
}


/// <summary>
/// GUIs the text.
/// </summary>
/// <param name="view">The view.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiText(U16View view) noexcept {
    // 只读
    if (m_info.flags & Flag_GuiReadOnly) return false;
    
    assert(!"NOT IMPL");
    return false;
}

/// <summary>
/// GUIs the return.
/// </summary>
/// <returns></returns>
bool RichED::CEDTextDocument::GuiReturn() noexcept {
    // 单行
    if (m_info.flags & Flag_MultiLine) {
        const char16_t line_feed[1] = { '\n' };
        return this->GuiText({ line_feed, line_feed + 1 });
    }
    return false;
}

// ----------------------------------------------------------------------------
//                                Set RichText Format
// ----------------------------------------------------------------------------

/// <summary>
/// Sets the riched.
/// </summary>
/// <param name="begin">The begin.</param>
/// <param name="end">The end.</param>
/// <param name="offset">The offset.</param>
/// <param name="size">The size.</param>
/// <param name="data">The data.</param>
/// <param name="relayout">if set to <c>true</c> [relayout].</param>
/// <returns></returns>
bool RichED::CEDTextDocument::set_riched(
    DocPoint begin, DocPoint end, 
    size_t offset, size_t size, 
    const void * data, bool relayout) noexcept {
    CheckRangeCtx ctx;
    // 检测范围合理性
    if (!Private::CheckRange(*this, begin, end, ctx)) return false;

    // 修改数据
    const auto set_data = [data, offset, size](CEDTextCell& cell) noexcept {
        auto& rd = const_cast<RichData&>(cell.GetRichED());
        char* const dst = reinterpret_cast<char*>(&rd);
        const char* const src = reinterpret_cast<const char*>(data); 
        std::memcpy(dst + offset, src, size);
        cell.AsDirty();
    };

    // 解包
    const auto cell1 = ctx.begin.cell;
    const auto pos1 = ctx.begin.offset;
    const auto cell2 = ctx.end.cell;
    const auto pos2 = ctx.end.offset;
    // 细胞分裂: 由于cell1可能等于cell2, 所以先分裂cell2
    const auto e = cell2->Split(pos2);
    const auto b = cell1->Split(pos1);
    if (b && e) {
        const auto cfor = detail::cfor_cells(b, e);
        RichED::NeedRedraw(*this);
        for (auto& cell : cfor) set_data(cell);
        // 重新布局
        if (relayout) Private::Dirty(*this, *cell1, begin.line);
        // 增量布局
        else {
            if (cell1 != b) {
                this->platform.RecreateContext(*cell1);
                b->metrics.pos = cell1->metrics.pos + cell1->metrics.width;
            }
            if (cell2 != e) {
                // 可能cell1 == cell2
                const auto current = static_cast<CEDTextCell*>(e->prev);
                this->platform.RecreateContext(*current);
                e->metrics.pos = current->metrics.pos + current->metrics.width;
            }
        }
        return true;
    }
    return false;
}


/// <summary>
/// Sets the flags.
/// </summary>
/// <param name="begin">The begin.</param>
/// <param name="end">The end.</param>
/// <param name="flags">The flags.</param>
/// <param name="set">The set.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::set_flags(
    DocPoint begin, DocPoint end, 
    uint16_t flags, uint32_t set) noexcept {
    CheckRangeCtx ctx;
    // 检测范围合理性
    if (!Private::CheckRange(*this, begin, end, ctx)) return false;
    // 解包
    const auto cell1 = ctx.begin.cell;
    const auto pos1 = ctx.begin.offset;
    const auto cell2 = ctx.end.cell;
    const auto pos2 = ctx.end.offset;

    // 修改的是effect还是flag?
    const size_t flag_offset
        = set & set_fflags
        ? offsetof(RichData, fflags)
        : offsetof(RichData, effect)
        ;
    // 重新布局
    const uint16_t change_font_flags = set & set_fflags;
    static_assert(sizeof(RichData::fflags) == sizeof(uint16_t), "same!");
    static_assert(sizeof(RichData::effect) == sizeof(uint16_t), "same!");
    // 获取标志
    const auto ref_flags = [flag_offset](CEDTextCell& cell) noexcept -> uint16_t& {
        auto& rd = const_cast<RichData&>(cell.GetRichED());
        char* const dst = reinterpret_cast<char*>(&rd);
        uint16_t* const rv = reinterpret_cast<uint16_t*>(dst + flag_offset);
        return *rv;
    };
    // 修改模式
    if (set & Set_Change) {
        // 检查修改点两端的情况: 两段都为true时修改为false, 否则修改为true;
        if ((ref_flags(*cell1) & ref_flags(*cell2) & flags) == flags)
            set = Set_False;
        else
            set = Set_True;
    }
    // 与或标志
    const uint16_t and_flags = ~flags;
    const uint16_t or__flags = set & Set_True ? flags : 0;
    // 修改数据
    const auto set_data = [=](CEDTextCell& cell) noexcept {
        auto& flags = ref_flags(cell);
        flags = (flags & and_flags) | or__flags;
    };
    // 细胞分裂: 由于cell1可能等于cell2, 所以先分裂cell2
    const auto e = cell2->Split(pos2);
    const auto b = cell1->Split(pos1);
    if (b && e) {
        const auto cfor = detail::cfor_cells(b, e);
        RichED::NeedRedraw(*this);
        for (auto& cell : cfor) set_data(cell);
        // 重新布局
        if (change_font_flags) {
            for (auto& cell : cfor) cell.AsDirty();
            Private::Dirty(*this, *cell1, begin.line);
        }
        // 增量布局
        else {
            if (cell1 != b) {
                this->platform.RecreateContext(*cell1);
                b->metrics.pos = cell1->metrics.pos + cell1->metrics.width;
            }
            if (cell2 != e) {
                // 可能cell1 == cell2
                const auto current = static_cast<CEDTextCell*>(e->prev);
                this->platform.RecreateContext(*current);
                e->metrics.pos = current->metrics.pos + current->metrics.width;
            }
        }
        return true;
    }
    return false;
}


// ----------------------------------------------------------------------------
//                      RichED::CEDTextDocument::Private
// ----------------------------------------------------------------------------


/// <summary>
/// Expands the visual-line clean area.
/// </summary>
/// <param name="doc">The document.</param>
/// <returns></returns>
void RichED::CEDTextDocument::Private::ExpandVL(
    CEDTextDocument& doc, Node& end, unit_t bottom) noexcept {
    auto& vlv = doc.m_vVisual;
    if (vlv.IsFailed()) return;
    assert(vlv.GetSize() && "bad size");
    // 保证第一个准确
    vlv[0].first = detail::next_cell(&doc.m_head);
    // 视口宽度, 用于自动换行
    const auto viewport_w = doc.m_rcViewport.width;
    // 获取
    const uint32_t count = vlv.GetSize() - 1;
    auto line = vlv[count];
    auto cell = line.first;
    // 已经处理完毕
    if (cell == &end) return;
    vlv.Resize(count);
    line.ar_height_max = line.dr_height_max = 0;
    unit_t offset_inline = 0;
    uint32_t char_length_vl = 0;
    //uintptr_t new_line = 0;
    // 起点为无效起点
    while (cell != &end) {
        /*
         1. 在'条件允许'下尝试合并后面的CELL
         2. 遍历需要换行CELL处(```x + w >= W```)
         3. (a.)如果是内联之类的特殊CELL, 或者换行处足够靠前:
                    要么换行, 要么保留(视觉行第一个)
         4. (b.)查询可换行处, 并分裂为A, B: B换行
        */


        // 尝试合并后CELL
        if (Private::Merge(doc, *cell, viewport_w, offset_inline)) 
            cell->MergeWithNext();

        bool new_line = cell->GetMetaInfo().eol;
        //

        // 重建脏CELL
        Private::Recreate(doc, *cell);
        // 自动换行
        const auto offset_end = offset_inline + cell->metrics.width;
        if (doc.m_info.wrap_mode && offset_end > viewport_w && cell->metrics.width > 0) {
            // 整个CELL换行
            if (cell->GetMetaInfo().metatype >= Type_UnknownInline ||
                offset_inline + cell->GetRichED().size > viewport_w ||
                Private::CheckWrap(doc, *cell, viewport_w - offset_inline) == cell
                ) {
                // -------------------------
                // --------------------- BOVL
                // -------------------------

                // 换行
                if (!detail::push_data(vlv, line)) return;
                cell->metrics.pos = 0;
                // 这里换行不是逻辑
                line.char_len_before += char_length_vl;
                char_length_vl = 0;
                offset_inline = 0;
                // 行偏移 = 上一行偏移 + 上一行最大升高 + 上一行最大降高
                line.first = cell;
                line.offset += line.ar_height_max + line.dr_height_max;
                line.ar_height_max = cell->metrics.ar_height;
                line.dr_height_max = cell->metrics.dr_height;
            }
            // 其他情况
            else {
                new_line = true;
                // 重建脏CELL
                if (cell->GetMetaInfo().dirty) doc.platform.RecreateContext(*cell);
            }
        }

        // --------------------------
        // --------------------- EOVL
        // --------------------------


        // 行内偏移
        cell->metrics.pos = offset_inline;
        offset_inline += cell->metrics.width;
        char_length_vl += cell->GetString().length;
        // 行内升部降部最大信息
        line.ar_height_max = std::max(cell->metrics.ar_height, line.ar_height_max);
        line.dr_height_max = std::max(cell->metrics.dr_height, line.dr_height_max);
        // 换行
        if (new_line) {
            if (!detail::push_data(vlv, line)) return;
            line.char_len_before += char_length_vl;
            char_length_vl = 0;
            line.lineno += cell->GetMetaInfo().eol;
            if (cell->GetMetaInfo().eol) line.char_len_before = 0;
            line.first = detail::next_cell(cell);
            // 行偏移 = 上一行偏移 + 上一行最大升高 + 上一行最大降高
            line.offset += line.ar_height_max + line.dr_height_max;
            line.ar_height_max = 0;
            line.dr_height_max = 0;
            offset_inline = 0;
            // 超过视口
            if (line.offset >= bottom) break;
        }
        // 推进
        cell = detail::next_cell(cell);
    }
    // 末尾
    push_data(vlv, line);
}


/// <summary>
/// Recreates the specified cell.
/// </summary>
/// <param name="cell">The cell.</param>
/// <returns></returns>
void RichED::CEDTextDocument::Private::Recreate(
    CEDTextDocument&doc, CEDTextCell& cell) noexcept {
    // 被注音的
    if (cell.GetMetaInfo().metatype == Type_UnderRuby) {
        // 第一次遍历: 重建检查是否重建
        bool need_create = false;
        const auto end_cell = [&cell, &need_create]() noexcept {
            need_create = cell.GetMetaInfo().dirty;
            auto node = detail::next_cell(&cell);
            //assert(node->GetMetaInfo().metatype == Type_Ruby);
            while (node->GetMetaInfo().metatype == Type_Ruby) {
                need_create |= node->GetMetaInfo().dirty;
                const auto eol = node->GetMetaInfo().eol;
                node = detail::next_cell(node);
                if (eol) break;
            }
            return node;
        }();
        if (!need_create) return;
        // 第二次遍历: 重建并检测宽度
        unit_t width = 0;
        {
            cell.AsDirty();
            doc.platform.RecreateContext(cell);
            auto node = detail::next_cell(&cell);
            // 没有注音
            if (node == end_cell) return;
            while (node != end_cell) {
                node->AsDirty();
                doc.platform.RecreateContext(*node);
                width += node->metrics.width;
                node = detail::next_cell(node);
            }
        }
        // 第三次遍历: 重构布局
        {
            auto node = detail::next_cell(&cell);
            const unit_t allw = std::max(width, cell.metrics.width);
            const unit_t offy =  -(/*node->metrics.dr_height + */cell.metrics.ar_height);
            const unit_t height = node->metrics.ar_height + node->metrics.dr_height;
            cell.metrics.offset.x = half(allw - cell.metrics.width);
            cell.metrics.offset.y = height;
            cell.metrics.ar_height += height;
            cell.metrics.width = allw;
            unit_t offset = -half(allw + width);
            while (node != end_cell) {
                node->metrics.offset.x = offset;
                node->metrics.offset.y = offy;
                offset += node->metrics.width;
                node->metrics.width = 0;
                node = detail::next_cell(node);
            }
        }
    }
    // 普通CELL
    else if (cell.GetMetaInfo().dirty) doc.platform.RecreateContext(cell);
}

/// <summary>
/// Merges the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="cell">The cell.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::Merge(
    CEDTextDocument & doc, CEDTextCell & cell, 
    unit_t width, unit_t offset) noexcept {
    if (!doc.m_info.wrap_mode) return true;
    if (cell.GetMetaInfo().eol) return false;
    const unit_t fs = cell.GetRichED().size;
    return offset + cell.metrics.width + fs < width;
}

/// <summary>
/// Checks the wrap.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="cell">The cell.</param>
/// <param name="pos">The position.</param>
/// <returns></returns>
auto RichED::CEDTextDocument::Private::CheckWrap(
    CEDTextDocument& doc, CEDTextCell& cell, unit_t pos) noexcept ->CEDTextCell* {
    const auto mode = doc.m_info.wrap_mode;
    const auto str = cell.GetString().data;
    const auto len = cell.GetString().length;
    switch (mode & 3)
    {
        CellHitTest hittest; uint32_t index;
    case Mode_NoWrap:
        break;
    case Mode_SpaceOnly:
        hittest = doc.platform.HitTest(cell, pos);
        // 向前查找空格
        index = hittest.pos;
        while (index--) {
            const auto ch = str[index];
            if (ch == ' ') return cell.Split(index + 1);
        }
        // 向后查找空格
        for (index = hittest.pos; index != len; ++index) {
            const auto ch = str[index];
            if (ch == ' ') return cell.Split(index + 1);
        }
        break;
    case Mode_SpaceOrCJK:
        hittest = doc.platform.HitTest(cell, pos);
        assert(hittest.pos < cell.GetString().length);
        // 向前查找空格、CJK
        index = hittest.pos;
        do {
            const auto this_index = index;
            // 空格允许延后一个字符
            const auto ch = str[this_index];
            if (ch == ' ') return cell.Split(this_index);
            // CJK需要提前一个字符
            char32_t cjk; const auto lch = str[this_index - 1];
            if (detail::is_high_surrogate(lch)) {
                assert(index); --index;
                const auto pch = str[this_index - 2];
                assert(detail::is_low_surrogate(pch));
                cjk = detail::char16x2to32(lch, pch);
            }
            else cjk = static_cast<char32_t>(lch);
            if (detail::is_cjk(cjk)) return cell.Split(this_index);
        } while (index--);
        // 向后查找空格、CJK
        for (index = hittest.pos; index != len; ++index) {
            const auto ch = str[index];
            if (ch == ' ') return cell.Split(index + 1);
            char32_t cjk;
            if (detail::is_low_surrogate(ch)) {
                const auto nch = str[++index];
                assert(detail::is_high_surrogate(nch));
                cjk = detail::char16x2to32(nch, ch);
            }
            else cjk = static_cast<char32_t>(ch);
            if (detail::is_cjk(cjk)) return cell.Split(index);
        }
        break;
    case Mode_Anywhere:
        hittest = doc.platform.HitTest(cell, pos);
        // 此处中断
        return cell.Split(hittest.pos);
    }
    return nullptr;
}


/// <summary>
/// Sets the selection.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="point">The point.</param>
/// <param name="mode">The mode.</param>
/// <param name="keepanchor">if set to <c>true</c> [keepanchor].</param>
/// <returns></returns>
void RichED::CEDTextDocument::Private::SetSelection(
    CEDTextDocument& doc, DocPoint point, 
    uint32_t mode, bool keepanchor) noexcept {
    const auto prev_caret = doc.m_dpCaret;
    // 设置选择区
    switch (mode)
    {
        uint32_t i;
    case impl::mode_all:
        // 全选: 锚点在最开始, 插入符在最后
        doc.m_dpAnchor = { 0, 0 };
        assert(doc.m_vLogic.GetSize());
        i = doc.m_vLogic.GetSize() - 1;
        doc.m_dpCaret = { i, doc.m_vLogic[i].length };
        break;
    case impl::mode_target:
        // 选中: 插入符更新至目标位置
        doc.m_dpCaret = point;
        break;
    case impl::mode_logicup:
        break;
    case impl::mode_logicdown:
        break;
    case impl::mode_logicleft:
        break;
    case impl::mode_logicright:
        break;
    }
    // 屏幕跟随插入符
}


/// <summary>
/// Inserts the text.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="dp">The dp.</param>
/// <param name="view">The view.</param>
/// <param name="linedata">The linedata.</param>
/// <param name="rich_ptr">The rich PTR.</param>
/// <param name="behind">if set to <c>true</c> [behind].</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::Insert(
    CEDTextDocument& doc, DocPoint dp,
    U16View view, LogicLine linedata, 
    bool behind) noexcept {
    // 需要重绘
    RichED::NeedRedraw(doc);
    // 最小化脏数组重建用数据
    //VisualLine vlbuffer[RED_DIRTY_ARRAY_SIZE];
    //uint32_t vlbuffer_length = 0;
    // 断言检测
    assert(GetLineTextLength(linedata.first) == linedata.length);
    assert(dp.pos <= linedata.length);
    assert(dp.line < doc.m_vLogic.GetSize());
    // TODO: 强异常保证
    // TODO: UNRO-REDO检查
    // TODO: [优化] 最小化脏数组重建
    // TODO: [优化] 仅插入换行符



    // 第二次遍历, 最小化m_vLogic重建
    auto pos = dp.pos;
    auto cell = linedata.first;
    // 遍历到合适的位置
    detail::find_cell1_txtoff_ex(cell, pos);
    // 这之后的为脏
    Private::Dirty(doc, *cell, dp.line);

    CellType insert_type = Type_Normal;

    // 检测是否能够插入


    // 1. 插入双字UTF16中间
    if (pos < cell->GetString().length) {
        if (detail::is_high_surrogate(cell->GetString().data[pos])) return false;
    }
    // 插在后面
    else {
        // BEHIND模式
        if (behind) {
            // 插入最后面
            if (cell->next == &doc.m_tail) {
                const auto obj = RichED::CreateNormalCell(doc, doc.default_riched);
                if (!obj) return false;
                RichED::InsertAfterFirst(*cell, *obj);
            }
            cell = detail::next_cell(cell);
            pos = 0; 
        }
        // 插入被注音后面算作注音
        switch (cell->GetMetaInfo().metatype)
        {
        case Type_Ruby:
        case Type_UnderRuby:
            insert_type = Type_Ruby;
        }
    }


    // 设置插入文字的格式. 注: 不要引用, 防止引用失效
    // 1. 通常: CELL自带格式
    // 2. 其他: 默认格式?
    const auto riched = *([=, &doc]() noexcept {
        return &cell->GetRichED();
        //return &doc.default_riched;
    }()); 



    // 第一次遍历, 为m_vLogic创建空间
    const auto lf_count = detail::lfcount(view);
    if (lf_count) {
        //[lf_count, &doc, dp, linedata]() noexcept {
        const size_t moved = sizeof(LogicLine) * (doc.m_vLogic.GetSize() - dp.line - 1);
        const auto ns = doc.m_vLogic.GetSize() + lf_count;
        if (!detail::resize_buffer(doc.m_vLogic, doc.platform, ns)) return false;
        const auto ptr = doc.m_vLogic.GetData();
        const auto base = ptr + dp.line;
        std::memmove(base + lf_count, base, moved);
        for (uint32_t i = 0; i != lf_count; ++i) ptr[i + 1] = { linedata.first, 0 };
        // 初始化行信息
        const uint32_t left = linedata.length - dp.pos;
        base[0].length = dp.pos;
        base[lf_count].length = left;
        //}();
    }




    // CELL是内联对象的情况
    //if (cell->GetMetaInfo().metatype >= Type_UnknownInline) {
    //    assert(cell->GetString().length == 1);
    //    assert(!"NOT IMPL");
    //}


    // 优化: 足够塞进去的话
    auto line_ptr = &doc.m_vLogic[dp.line];
    if (!lf_count) {
        const auto len = view.second - view.first;
        if (len <= cell->GetString().Left()) {
            cell->InsertText(pos, view);
            line_ptr->length += len;
            Private::Dirty(doc, *cell, dp.line);
            return true;
        }
    }




    // 1. 插入空的CELL的内部, 一定是行首
    // 2. 插入非空CELL的前面, 一定是行首
    // 3. 插入非空CELL的中间
    // 4. 插入非空CELL的后面


    // 插入前面: 创建新CELL[A], 插入点为B, 需要替换行首CELL信息
    // 插入中间: 分裂为两个[A, B]
    // 插入后面: 视为分裂

    // 插入字符串 分解成三部分:
    // 1. 字符串(前面)够能插入A的部分
    // 2. 字符串(后面)能够插入B的部分(可能与1部分重叠, 需要消除重叠)
    // 3. 中间剩余的部分(可能不存在)

    CEDTextCell* cell_a, *cell_b;
    Node** pointer_to_the_first_at_line = &line_ptr->first->prev->next;

    // 前面
    if (pos == 0) {
        cell_a = RichED::CreateNormalCell(doc, riched);
        if (!cell_a) return false;
        cell_b = cell;
        const auto prev_cell = static_cast<CEDTextCell*>(cell_b->prev);
        RichED::InsertAfterFirst(*prev_cell, *cell_a);
    }
    // 细胞分裂
    else {
        cell_b = cell->SplitEx(pos);
        if (!cell_b) return false;
        const_cast<CellMeta&>(cell_b->GetMetaInfo()).metatype = insert_type;
        cell_a = cell;
    }
    //cells = { cell_a, cell_b };
    // 对其进行插入
    const auto view1 = detail::nice_view1(view, cell_a->GetString().Left());
    const auto view2 = detail::nice_view2(view, cell_b->GetString().Left());

    line_ptr[0].first = static_cast<CEDTextCell*>(*pointer_to_the_first_at_line);;
    line_ptr[0].length += view1.second - view1.first;
    line_ptr[lf_count].length += view2.second - view2.first;
    cell_a->InsertText(pos, view1);
    cell_b->InsertText(0, view2);
    cell = cell_a;


    // 中间字符
    if (view.first != view.second) {
        while (true) {
            auto line_view = detail::lfview(view);
            do {
                auto this_end = line_view.first + TEXT_CELL_STR_MAXLEN;
                // 越界
                if (this_end > line_view.second) this_end = line_view.second;
                // 双字检查
                if (detail::is_low_surrogate(this_end[-1])) ++this_end;
                // 创建CELL
                const auto obj = RichED::CreateNormalCell(doc, riched);
                if (!obj) return false;
                const_cast<CellMeta&>(obj->GetMetaInfo()).metatype = insert_type;
                line_ptr->length += this_end - line_view.first;
                obj->InsertText(0, { line_view.first, this_end });
                RichED::InsertAfterFirst(*cell, *obj);
                cell = obj;
                line_view.first = this_end;
            } while (line_view.first < line_view.second);
            // 换行

            line_ptr->first = static_cast<CEDTextCell*>(*pointer_to_the_first_at_line);
            pointer_to_the_first_at_line = &cell->next;
            ++line_ptr;
            if (view.first == view.second) break;
            cell->AsEOL();
        }
        // 最后一个换行
        if (view.second[-1] == '\n') {
            line_ptr->first = static_cast<CEDTextCell*>(*pointer_to_the_first_at_line);
            cell->AsEOL();
        }
    }
    //doc.m_vLogic;
    return true;
}


/// <summary>
/// Inserts the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="dp">The dp.</param>
/// <param name="obj">The object.</param>
/// <param name="line_data">The line data.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::Insert(
    CEDTextDocument& doc, DocPoint dp, CEDTextCell& obj, 
    LogicLine& line_data) noexcept {
    // 需要重绘
    RichED::NeedRedraw(doc);
    assert(obj.GetMetaInfo().eol == false && "cannot insert EOL");
    auto pos = dp.pos;
    auto cell = line_data.first;
    // 遍历到合适的位置
    while (pos > cell->GetString().length) {
        pos -= cell->GetString().length;
        cell = detail::next_cell(cell);
    }
    // 遍历到合适的位置
    detail::find_cell1_txtoff_ex(cell, pos);
    // 必须是正常的
    assert(pos == 0 || pos == cell->GetString().length || cell->GetMetaInfo().metatype == Type_Normal);

    // 插入前面 更新行首
    // 插入后面 添加
    // 插入中间 分裂


    auto insert_after_this = cell;
    // 这之后的为脏
    Private::Dirty(doc, *cell, dp.line);
    auto const next_is_first = line_data.first->prev;

    if (pos == 0) insert_after_this = static_cast<CEDTextCell*>(cell->prev);
    else if (pos < cell->GetString().length) if (!cell->Split(pos)) return false;
    RichED::InsertAfterFirst(*insert_after_this, obj);

    line_data.first = detail::next_cell(next_is_first);
    line_data.length += obj.GetString().length;
    return true;
}




/// <summary>
/// Removes the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="begin">The begin.</param>
/// <param name="end">The end.</param>
/// <param name="">The .</param>
/// <param name="ctx">The CTX.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::Remove(
    CEDTextDocument& doc, DocPoint begin, DocPoint end,
    const CheckRangeCtx& ctx) noexcept {
    // 解包
    const auto cell1 = ctx.begin.cell;
    const auto pos1 = ctx.begin.offset;
    const auto cell2 = ctx.end.cell;
    const auto pos2 = ctx.end.offset;
    auto& line_data1 = *ctx.line1;
    const auto line_data2 = ctx.line2;
    const auto next_is_first_to_line_1 = line_data1.first->prev;
    // 需要重绘
    RichED::NeedRedraw(doc);
    // 标记为脏
    Private::Dirty(doc, *cell1, begin.line);
    // 处理
    const auto cell2_next = detail::next_cell(cell2);
    // 删除的是同一个CELL
    if (cell1 == cell2) {
        cell1->RemoveTextEx({ pos1, pos2 - pos1 });
    }
    else {
        // 直接释放(CELL1, CELL2)
        for (auto node = cell1->next; node != cell2; ) {
            const auto next_node = node->next;
            const auto cell_obj = static_cast<CEDTextCell*>(node);
            node = next_node;
            cell_obj->Dispose();
        }
        // 连接CELL1, CELL2
        cell1->next = cell2;
        cell2->prev = cell1;
        // CELL1: 删除[pos1, end)
        // CELL2: 删除[0, pos2)
        cell1->RemoveTextEx({ pos1, cell1->GetString().length - pos1 });
        // EOL检测
        const bool delete_eol = cell2->GetMetaInfo().eol && pos2 == cell2->GetString().length;
        cell2->RemoveTextEx({ 0, pos2 });
        // EOL恢复
        if (delete_eol) {
            auto node = static_cast<CEDTextCell*>(cell2_next->prev);
            // 从头删除
            if (begin.pos == 0) {
                // 重新创建一个CELL作为
                const auto ptr = RichED::CreateNormalCell(doc, doc.default_riched);
                // TODO: 错误处理
                if (!ptr) return false;
                RichED::InsertAfterFirst(*node, *ptr);
                node = ptr;
            }
            node->AsEOL();
        }
    }
    // 长度计算: line_data1可能与line_data2一致(大概率, 同一行修改)
    const auto old_len2 = line_data2.length;
    line_data1.length = begin.pos;
    line_data1.length += line_data2.length - end.pos;
    // 行首计算
    line_data1.first = detail::next_cell(next_is_first_to_line_1);
    // 合并逻辑行
    if (begin.line != end.line) {
        auto& llv = doc.m_vLogic;
        const auto len = llv.GetSize();
        const auto ptr = llv.GetData();
        const auto bsize = sizeof(ptr[0]) * (len - end.line);
        std::memmove(ptr + begin.line, ptr + end.line, bsize);
        llv.ReduceSize(len + begin.line - end.line);
    }
    return true;
}


/// <summary>
/// Dirties the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="cell">The cell.</param>
/// <param name="logic_line">The logic line.</param>
/// <returns></returns>
void RichED::CEDTextDocument::Private::Dirty(CEDTextDocument& doc, CEDTextCell& cell, uint32_t logic_line) noexcept {
    auto& vlv = doc.m_vVisual;
    const auto size = vlv.GetSize();
    assert(size);
    // 大概率在编辑第一行, 直接返回
    if (size < 2) return;
    // 利用二分查找到第一个, 然后, 删掉后面的
    const auto cmp = [](const VisualLine& vl, uint32_t ll) noexcept { return vl.lineno < ll; };
    const auto itr = std::lower_bound(vlv.begin(), vlv.end(), logic_line, cmp);
    const uint32_t index = itr - vlv.begin();
    if (index < size) vlv.ReduceSize(index + 1);
}

/// <summary>
/// Checks the range.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="begin">The begin.</param>
/// <param name="end">The end.</param>
/// <param name="ctx">The CTX.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::CheckRange(
    CEDTextDocument& doc, DocPoint begin, DocPoint end, CheckRangeCtx& ctx) noexcept {
    const auto line = doc.m_vLogic.GetSize();
    if (end.line < line) {
        auto& line_data1 = doc.m_vLogic[begin.line];
        auto& line_data2 = doc.m_vLogic[end.line];
        // 范围钳制
        begin.pos = std::min(begin.pos, line_data1.length);
        end.pos = std::min(end.pos, line_data2.length);
        // 没得删
        if (Cmp(end) > Cmp(begin)) {
            // 断言检测
            assert(GetLineTextLength(line_data1.first) == line_data1.length);
            assert(begin.pos <= line_data1.length);
            assert(begin.line < doc.m_vLogic.GetSize());
            assert(GetLineTextLength(line_data2.first) == line_data2.length);
            assert(end.pos <= line_data2.length);
            assert(end.line < doc.m_vLogic.GetSize());
            // 遍历到需要的位置
            auto pos1 = begin.pos;
            auto cell1 = line_data1.first;
            auto pos2 = end.pos;
            auto cell2 = line_data2.first;
            detail::find_cell2_txtoff_ex(cell1, pos1);
            detail::find_cell1_txtoff_ex(cell2, pos2);
            assert(cell1 != cell2 || pos1 != pos2);
            // 删除无效区间
            if (detail::is_high_surrogate(cell1->GetString().data[pos1])) return false;
            if (pos2 < cell2->GetString().length)
                if (detail::is_high_surrogate(cell2->GetString().data[pos2])) return false;
            ctx.begin = { cell1, pos1 };
            ctx.end = { cell2, pos2 };
            ctx.line1 = &line_data1;
            ctx.line2 = line_data2;
            return true;
        }
    }
    return false;
}


/// <summary>
/// Hits the test.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="pos">The position.</param>
/// <param name="ctx">The CTX.</param>
/// <returns></returns>
bool RichED::CEDTextDocument::Private::HitTest(
    CEDTextDocument & doc, Point pos, HitTestCtx& ctx) noexcept {
    // 数据无效
    auto& vlv = doc.m_vVisual;
    // 最后一行是无效数据
    if (vlv.GetSize() < 2) return false;
    // TODO: 固定行高模式
    if (doc.m_info.flags & Flag_FixedLineHeight) {
        assert(!"NOT IMPL");
        return false;
    }
    // 二分查找到指定视觉行
    
    const auto cmp = [](unit_t ll, const VisualLine& vl) noexcept { return  ll < vl.offset; };
    const auto itr = std::upper_bound(vlv.begin(), vlv.end(), pos.y, cmp);
    // 正常情况下, itr指向的是下一行. 比如: [0, 20, 40]中, 输入10输出指向20
    int offset = -1;
    // 太高的话算第一行
    if (itr == vlv.begin()) offset = 0;
    // 太低的话算倒数第二行(最后一行是END-CELL)
    else if (itr == vlv.end()) offset = -2;
    // 获取指定信息
    const auto& line0 = itr[offset];
    ctx.visual_line = &line0;
    uint32_t char_offset_in_line = line0.char_len_before;
    const auto& line1 = itr[offset + 1];
    unit_t offthis = pos.x;
    const auto cfor = detail::cfor_cells(line0.first, line1.first);
    auto target = static_cast<CEDTextCell*>(line1.first->prev);
    // 遍历到指定位置
    for (auto& cell : cfor) {
        if (offthis < cell.metrics.width) {
            target = &cell;
            break;
        }
        char_offset_in_line += cell.GetString().length;
        offthis -= cell.metrics.width;
    }
    // 进行点击测试
    const auto ht = doc.platform.HitTest(*target, offthis);
    ctx.text_cell = target;
    ctx.len_before_cell = char_offset_in_line;
    ctx.len_in_cell = ht.pos + ht.trailing * ht.length;
    return true;
}