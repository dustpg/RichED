#pragma once
/**
* Copyright (c) 2018-2019 dustpg   mailto:dustpg@gmail.com
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/


#include "ed_common.h"
#include "ed_txtbuf.h"
#include "ed_undoredo.h"
#include <cstddef>

// riched namespace
namespace RichED {
    // text platform
    struct IEDTextPlatform;
    // text cell
    class CEDTextCell;
    // logic line(LL) data
    struct LogicLine {
        // first cell
        CEDTextCell*    first;
        // text length, LF not included
        uint32_t        length;
    };
    // visual line(VL) data
    struct VisualLine {
        // first cell
        CEDTextCell*    first;
        // logic line 
        uint32_t        lineno;
        // char offset for this logic line, 0 for first VL in LL
        uint32_t        char_len_before;
        // char offset for this logic line, 0 for first VL in LL
        uint32_t        char_len_this;
        // offset for this visual-line
        unit_t          offset;
        // max ascender-height in this visual-line
        unit_t          ar_height_max;
        // max deascender-height in this visual-line
        unit_t          dr_height_max;
    };
    // functions
    //using funcptr_t = void(*)(int);
    // text document
    class CEDTextDocument {
        // flag set private
        enum flag_set : uint32_t { set_effect = 0 << 2, set_fflags = 1 << 2 };
        // set riched
        bool set_riched(
            DocPoint begin, DocPoint end,
            size_t offset, size_t size, 
            const void* data, bool relayout
        ) noexcept;
        // set flags
        bool set_flags(
            DocPoint begin, DocPoint end,
            uint16_t flags,  uint32_t set
        ) noexcept;
    public:
        // private impl
        struct Private;
        // ctor
        CEDTextDocument(IEDTextPlatform&, const DocInitArg&) noexcept;
        // ctor
        ~CEDTextDocument() noexcept;
        // no copy ctor
        CEDTextDocument(const CEDTextDocument&) noexcept = delete;
        // update
        void BeforeRender() noexcept;
        // render
        void Render() noexcept;
        // set doc view-point pos
        void SetPos(Point) noexcept;
        // resize doc view-zone
        void Resize(Size) noexcept;
        // save to bin-file for self-use
        bool SaveBinFile(CtxPtr) noexcept;
        // load from bin-file for self-use
        bool LoadBinFile(CtxPtr) noexcept;
        // gen text
        void GenText(CtxPtr ctx, DocPoint begin, DocPoint end)noexcept;
        // get logic line count 
        auto GetLogicLineCount() const noexcept { return m_vLogic.GetSize(); }
        // get selection
        auto&RefSelection() const noexcept { return m_vSelection; }
        // get caret rect under doc space
        auto GetCaret() const noexcept { return m_rcCaret; };
        // get line feed data
        auto&RefLineFeed() const noexcept { return m_linefeed; }
        // get info
        auto&RefInfo() const noexcept { return m_info; }
        // set new line feed
        void SetLineFeed(LineFeed) noexcept;
        // set new wrap mode
        //void SetWrapMode(WrapMode) noexcept;
        // get selection
        auto GetSelectionRange() const noexcept { return DocRange{ m_dpSelBegin, m_dpSelEnd }; }
    public:
        // begin an operation for undo-stack
        void BeginOp() noexcept;
        // end an operation for undo-stack
        void EndOp() noexcept;
        // force set & update caret anchor
        void SetAnchorCaret(DocPoint anchor, DocPoint caret) noexcept;
        // create inline object
        auto CreateInlineObject(const InlineInfo&, int16_t len, CellType type) noexcept->CEDTextCell*;
        // insert inline object
        bool InsertInline(DocPoint, CEDTextCell&&) noexcept;
        // insert inline object
        bool InsertInline(DocPoint dp, CEDTextCell* p) noexcept { return p ? InsertInline(dp, std::move(*p)) : false; }
        // insert ruby
        bool InsertRuby(DocPoint, char32_t, U16View, const RichData* = nullptr) noexcept;
        // insert text, pos = min(DocPoint::pos, line-length)
        auto InsertText(DocPoint, U16View, bool behind =false) noexcept ->DocPoint;
        // remove text, pos = min(DocPoint::pos, line-length)
        bool RemoveText(DocPoint begin, DocPoint end) noexcept;
    public: // Rich Text Format
        // type ref
        using color_t = decltype(RichData::color);
        using fname_t = decltype(RichData::name);
        enum FlagSet : uint32_t { Set_False = 0 << 0, Set_True = 1 << 0, Set_Change = 1 << 1 };
        // set riched
        bool SetRichED(DocPoint begin, DocPoint end, const RichData& rd) noexcept {
            return set_riched(begin, end, 0, sizeof(RichData), &rd, true); }
        // set font size
        bool SetFontSize(DocPoint begin, DocPoint end, const unit_t& fs) noexcept {
            return set_riched(begin, end, offsetof(RichData, size), sizeof(fs), &fs, true); }
        // set font color
        bool SetFontColor(DocPoint begin, DocPoint end, const color_t& fc) noexcept {
            return set_riched(begin, end, offsetof(RichData, color), sizeof(fc), &fc, false); }
        // set font name
        bool SetFontName(DocPoint begin, DocPoint end, const fname_t& fn) noexcept {
            return set_riched(begin, end, offsetof(RichData, name), sizeof(fn), &fn, true); }
        // set under line
        bool SetUnerline(DocPoint begin, DocPoint end, FlagSet set) noexcept {
            return set_flags(begin, end, Effect_Underline, set | set_effect); }
        // set italic
        bool SetItalic(DocPoint begin, DocPoint end, FlagSet set) noexcept {
            return set_flags(begin, end, FFlags_Italic, set | set_fflags); }
    public: // GUI Operation, return false on gui-level mistake
        // gui: l-button up
        //bool GuiLButtonUp(Point pt) noexcept;
        // gui: l-button down
        bool GuiLButtonDown(Point pt, bool shift) noexcept;
        // gui: l-button hold&move
        bool GuiLButtonHold(Point pt) noexcept;
        // gui: char
        bool GuiChar(char32_t ch) noexcept;
        // gui: text
        bool GuiText(U16View view) noexcept;
        // gui: return/enter
        bool GuiReturn() noexcept;
        // gui: backspace
        bool GuiBackspace(bool ctrl) noexcept;
        // gui: delete
        bool GuiDelete(bool ctrl) noexcept;
        // gui: left
        bool GuiLeft(bool ctrl, bool shift) noexcept;
        // gui: right
        bool GuiRight(bool ctrl, bool shift) noexcept;
        // gui: up
        bool GuiUp(bool ctrl, bool shift) noexcept;
        // gui: down
        bool GuiDown(bool ctrl, bool shift) noexcept;
        // gui: home
        bool GuiHome(bool ctrl, bool shift) noexcept;
        // gui: end
        bool GuiEnd(bool ctrl, bool shift) noexcept;
        // gui: page up
        bool GuiPageUp(bool ctrl, bool shift) noexcept;
        // gui: page down
        bool GuiPageDown(bool ctrl, bool shift) noexcept;
        // gui: select all
        bool GuiSelectAll() noexcept;
        // gui: undo
        bool GuiUndo() noexcept;
        // gui: redo
        bool GuiRedo() noexcept;
    public: // GUI Operation - for Rich Text
        // font size
        //bool GuiFontSize() noexcept;
    public: // helper
        // valign helper h
        void VAlignHelperH(unit_t ar, unit_t height, CellMetrics& m) noexcept;
    public:
        // platform
        IEDTextPlatform&        platform;
        // default riched
        RichData                default_riched;
    private:
        // undo stack
        CEDUndoRedo             m_undo;
        // matrix
        DocMatrix               m_matrix;
        // normal info
        DocInfo                 m_info;
        // linefeed data
        LineFeed                m_linefeed;
        // viewport
        Rect                    m_rcViewport;
        // caret rect
        Rect                    m_rcCaret;
        // document estimate size
        Size                    m_szEstimated;
        // anchor pos
        DocPoint                m_dpAnchor;
        // caret pos
        DocPoint                m_dpCaret;
        // selection begin
        DocPoint                m_dpSelBegin;
        // selection end
        DocPoint                m_dpSelEnd;
        // undo op
        uint16_t                m_uUndoOp = 0;
        // changed flag
        uint16_t                m_flagChanged = 0;
        // unused
        uint16_t                m_unused_16x3[3];
        // head
        Node                    m_head;
        // tail
        Node                    m_tail;
#ifndef NDEBUG
        // debug buffer to avoid head/tail node as a cell
        char                    m_dbgBuffer[128];
#endif
        // visual lines cache
        CEDBuffer<VisualLine>   m_vVisual;
        // logic line data
        CEDBuffer<LogicLine>    m_vLogic;
        // selection data
        CEDBuffer<Box>          m_vSelection;
    };
}
