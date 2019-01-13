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

// riched namespace
namespace RichED {
    // cell
    class CEDTextCell;
    // text platform
    struct PCN_NOVTABLE IEDTextPlatform {
        // value changed
        enum Changed: uint32_t {
            // view changed, need redraw
            Changed_View = 0,
            // selection changed
            Changed_Selection,
            // caret changed
            Changed_Caret,
            // text changed
            Changed_Text,
            // estimated width changed
            Changed_EstimatedWidth,
            // estimated height changed
            Changed_EstimatedHeight,
        };
        // error beep
        virtual void ErrorBeep() noexcept = 0;
        // on out of memory, won't be called on Doc's ctor
        virtual auto OnOOM(uint32_t retry_count) noexcept ->HandleOOM = 0;
        // value changed
        virtual void ValueChanged(Changed) noexcept = 0;
        // is valid password
        virtual bool IsValidPassword(char32_t) noexcept = 0;
        // generate text
        virtual void GenerateText(void* string, U16View view) noexcept = 0;
        // recreate context [split?]
        virtual void RecreateContext(CEDTextCell& cell) noexcept = 0;
        // delete context
        virtual void DeleteContext(CEDTextCell&) noexcept = 0;
        // draw context
        virtual void DrawContext(CEDTextCell&, unit_t baseline) noexcept = 0;
        // hit test
        virtual auto HitTest(CEDTextCell&, unit_t offset) noexcept->CellHitTest = 0;
#ifndef NDEBUG
        // debug output
        virtual void DebugOutput(const char*) noexcept = 0;
#endif
    };
}
