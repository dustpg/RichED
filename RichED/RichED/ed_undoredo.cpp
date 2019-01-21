#include "ed_undoredo.h"
#include "ed_txtdoc.h"

#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <algorithm>


namespace RichED {
    /// <summary>
    /// Undoes the redo idle.
    /// </summary>
    /// <param name="doc">The document.</param>
    /// <param name="op">The op.</param>
    /// <returns></returns>
    void UndoRedoIdle(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept { }
    // init callback
    void InitCallback(TrivialUndoRedo& op) noexcept;
    // private impl
    struct CEDTextDocument::Private {
        // set caret data
        static void AnchorCaret(CEDTextDocument& doc,  TrivialUndoRedo& op) noexcept;
    };
}


/// <summary>
/// Initializes a new instance of the <see cref="CEDUndoRedo"/> class.
/// </summary>
/// <param name="max_deep">The maximum deep.</param>
RichED::CEDUndoRedo::CEDUndoRedo(uint32_t max_deep) noexcept : max_deep(max_deep) {
    // 处理Cell节点
    m_head.prev = nullptr;
    m_head.next = &m_tail;
    m_tail.prev = &m_head;
    m_tail.next = nullptr;
#ifndef NDEBUG
    std::memset(m_dbgBuffer, 233, sizeof(m_dbgBuffer));
#endif
    static_assert(std::is_trivial<TrivialUndoRedo>::value == true, "Trivial!");
}


/// <summary>
/// Clears this instance.
/// </summary>
/// <returns></returns>
void RichED::CEDUndoRedo::Clear() noexcept {
    auto node = m_head.next;
    while (node != &m_tail) {
        const auto ptr = node;
        node = node->next;
        std::free(ptr);
    }
    m_head.next = m_pStackTop = &m_tail;
    m_tail.prev = &m_head;
    m_cCurrent = 0;
}

/// <summary>
/// Undoes the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <returns></returns>
bool RichED::CEDUndoRedo::Undo(CEDTextDocument& doc) noexcept {
    auto node = m_pStackTop;
    // 撤销栈为空
    if (node == &m_tail) return false;
    TrivialUndoRedo* last = nullptr;
    while (true) {
        const auto op = static_cast<TrivialUndoRedo*>(node);
        last = op;
        op->undo(doc, *op);
        if (!op->decorator) break;
        node = node->next;
    }
    doc.SetAnchorCaret(last->anchor, last->caret);
    m_pStackTop = node->next;
    return true;
}

/// <summary>
/// Redoes the specified document.
/// </summary>
/// <param name="doc">The document.</param>
/// <returns></returns>
bool RichED::CEDUndoRedo::Redo(CEDTextDocument & doc) noexcept {
    auto node = m_pStackTop;
    const auto first = m_head.next;
    // 撤销栈已满
    if (node == first) return false;
    TrivialUndoRedo* last = nullptr;
    while (true) {
        node = node->prev;
        const auto op = static_cast<TrivialUndoRedo*>(node);
        last = op;
        op->redo(doc, *op);
        // 到头了
        if (op == first) break;
        // 或者下(其实是上) 一个是非装饰操作
        if (!static_cast<TrivialUndoRedo*>(node->prev)->decorator) break;
    }
    doc.SetAnchorCaret(last->anchor, last->caret);
    m_pStackTop = node;
    return true;
}

/// <summary>
/// Anchors the caret.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="op">The op.</param>
/// <returns></returns>
void RichED::CEDTextDocument::Private::AnchorCaret(
    CEDTextDocument & doc, TrivialUndoRedo & op) noexcept {
    op.anchor = doc.m_dpAnchor;
    op.caret = doc.m_dpCaret;
}

/// <summary>
/// Adds the op.
/// </summary>
/// <param name="doc">The document.</param>
/// <param name="op">The op.</param>
/// <returns></returns>
void RichED::CEDUndoRedo::AddOp(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept {
    CEDTextDocument::Private::AnchorCaret(doc, op);
    // 释放HEAD -> TOP
    while (m_head.next != m_pStackTop) {
        const auto ptr = m_head.next;
        //const auto obj = static_cast<TrivialUndoRedo*>(ptr);
        m_head.next = m_head.next->next;
        std::free(ptr);
    }

    RichED::InitCallback(op);
    RichED::InsertAfterFirst(m_head, op);
    m_pStackTop = m_head.next;
}

// ----------------------------------------------------------------------------
//                               Ruby Char Insert
// ----------------------------------------------------------------------------

namespace RichED {
    // singe op for ruby
    struct RubySingeOp {
        // under riched
        RichData        under;
        // ruby riched
        RichData        ruby;
        // point
        DocPoint        point;
        // length of ruby
        uint32_t        ruby_length;
        // under char
        char16_t        under_char[2];
        // ruby char
        char16_t        ruby_char[2];
    };
}

// ----------------------------------------------------------------------------
//                             RichED Rich
// ----------------------------------------------------------------------------


namespace RichED {
    // singe op for rich
    struct RichSingeOp {
        // under riched
        RichData        riched;
        // begin point
        DocPoint        begin;
        // end point
        DocPoint        end;
    };
    // group op for rich
    struct RichGroupOp {
        // op for rollback
        RichSingeOp     back;
        // ops for execute
        RichSingeOp     exec[1];
    };
    // Rollback rich
    void RollbackRich(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept {
        const auto data = reinterpret_cast<RichGroupOp*>(&op + 1);
        doc.SetRichED(data->back.begin, data->back.end, data->back.riched);
    }
    // execute rich
    void ExecuteRich(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept {
        const auto data = reinterpret_cast<RichGroupOp*>(&op + 1);
        const auto end_ptr = reinterpret_cast<char*>(&op.bytes_from_here) + op.bytes_from_here;
        const auto end_itr = reinterpret_cast<RichSingeOp*>(end_ptr);
        std::for_each(data->exec, end_itr, [&doc](const RichSingeOp& op)noexcept {
            doc.SetRichED(op.begin, op.end, op.riched);
        });
    }
}


// ----------------------------------------------------------------------------
//                             RichED Text
// ----------------------------------------------------------------------------

namespace RichED {
    // group op for text
    struct TextGroupOp {
        // begin point
        DocPoint        begin;
        // end point
        DocPoint        end;
        // text length
        uint32_t        length;
        // text data
        char16_t        text[2];
    };
    // Rollback text
    void RollbackText(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept {
        const auto data = reinterpret_cast<TextGroupOp*>(&op + 1);
        doc.RemoveText(data->begin, data->end);
    }
    // execute text
    void ExecuteText(CEDTextDocument& doc, TrivialUndoRedo& op) noexcept {
        const auto data = reinterpret_cast<TextGroupOp*>(&op + 1);
        doc.InsertText(data->begin, { data->text, data->text + data->length });
    }
}

// ----------------------------------------------------------------------------
//                             RichED Overview
// ----------------------------------------------------------------------------

namespace RichED {
    // undo op
    enum UndoRedoOp : uint16_t {
        // remove: text
        Op_RemoveText = 0,
        // remove: rich
        Op_RemoveRich,
        // remove: objs
        // remove: ruby

        // insert: text
        Op_InsertText
    };
    namespace impl {
        /// <summary>
        /// Riches the undoredo.
        /// </summary>
        /// <param name="count">The count.</param>
        /// <returns></returns>
        void* rich_undoredo(uint32_t count) noexcept {
            assert(count);
            const size_t len = sizeof(TrivialUndoRedo)
                + sizeof(RichSingeOp)
                + sizeof(RichSingeOp) * count
                ;
            const auto ptr = std::malloc(len);
            if (ptr) {
                const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
                const size_t offset = offsetof(TrivialUndoRedo, bytes_from_here);
                op->bytes_from_here = len - offset;
            }
            return ptr;
        }
        /// <summary>
        /// Riches as remove.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <returns></returns>
        void rich_as_remove(void* ptr, uint16_t id) noexcept {
            assert(ptr && id);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            op->type = Op_RemoveRich;
            op->decorator = id - 1;
        }
        /// <summary>
        /// Riches the set.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <param name="index">The index.</param>
        /// <param name="data">The data.</param>
        /// <returns></returns>
        void rich_set(void* ptr, uint32_t index, const RichData & data, DocPoint a, DocPoint b) noexcept {
            assert(ptr);
            assert(a.line != b.line || a.pos != b.pos);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            const auto ops = reinterpret_cast<RichGroupOp*>(op + 1);
            ops->exec[index].riched = data;
            ops->exec[index].begin = a;
            ops->exec[index].end = b;
        }
    }
    namespace impl {
        /// <summary>
        /// Riches the undoredo.
        /// </summary>
        /// <param name="count">The count.</param>
        /// <returns></returns>
        void*text_undoredo(uint32_t count) noexcept {
            assert(count);
            const size_t len = sizeof(TrivialUndoRedo)
                + sizeof(TextGroupOp)
                + sizeof(char16_t) * count
                ;
            const auto ptr = std::malloc(len);
            if (ptr) {
                const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
                const size_t offset = offsetof(TrivialUndoRedo, bytes_from_here);
                op->bytes_from_here = len - offset;
                const auto ops = reinterpret_cast<TextGroupOp*>(op + 1);
                ops->length = count;
            }
            return ptr;
        }
        /// <summary>
        /// Texts as remove.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <param name="id">The identifier.</param>
        /// <param name="a">a.</param>
        /// <param name="b">The b.</param>
        /// <returns></returns>
        void text_as_remove(void* ptr, uint16_t id, DocPoint a, DocPoint b) noexcept {
            assert(ptr && id);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            op->type = Op_RemoveText;
            op->decorator = id - 1;
            const auto ops = reinterpret_cast<TextGroupOp*>(op + 1);
            ops->begin = a;
            ops->end = b;
        }
        /// <summary>
        /// Texts as insert.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <param name="id">The identifier.</param>
        /// <param name="a">a.</param>
        /// <param name="b">The b.</param>
        /// <returns></returns>
        void text_as_insert(void* ptr, uint16_t id, DocPoint a, DocPoint b) noexcept {
            assert(ptr && id);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            op->type = Op_InsertText;
            op->decorator = id - 1;
            const auto ops = reinterpret_cast<TextGroupOp*>(op + 1);
            ops->begin = a;
            ops->end = b;
        }
        /// <summary>
        /// Texts the append.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <param name="i">The i.</param>
        /// <param name="view">The view.</param>
        /// <returns></returns>
        void text_append(void* ptr, uint32_t i, U16View view) noexcept {
            assert(ptr);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            const auto ops = reinterpret_cast<TextGroupOp*>(op + 1);
            const auto len = view.second - view.first;
            std::memcpy(ops->text + i, view.first, len * sizeof(char16_t));
#ifndef NDEBUG
            // 调试时添加NUL字符方便调试
            ops->text[i + len] = 0;
#endif 
        }
        /// <summary>
        /// Texts the append.
        /// </summary>
        /// <param name="ptr">The PTR.</param>
        /// <param name="i">The i.</param>
        /// <param name="ch">The ch.</param>
        /// <returns></returns>
        void text_append(void* ptr, uint32_t i, char16_t ch) noexcept {
            assert(ptr);
            const auto op = reinterpret_cast<TrivialUndoRedo*>(ptr);
            const auto ops = reinterpret_cast<TextGroupOp*>(op + 1);
            ops->text[i] = ch;
#ifndef NDEBUG
            // 调试时添加NUL字符方便调试
            ops->text[i + 1] = 0;
#endif 
        }
    }
    /// <summary>
    /// Initializes the callback.
    /// </summary>
    /// <param name="op">The op.</param>
    /// <returns></returns>
    void InitCallback(TrivialUndoRedo& op) noexcept {
        switch (op.type)
        {
        default:
            assert(!"UNKNOWN!");
            op.undo = RichED::UndoRedoIdle;
            op.redo = RichED::UndoRedoIdle;
            break;
        case Op_RemoveText:
            // 移除文本
            // - 撤销: 文本添加
            // - 重做: 文本移除
            op.undo = RichED::ExecuteText;
            op.redo = RichED::RollbackText;
            break;
        case Op_RemoveRich:
            // 移除富属性
            // - 撤销: 富属性的修改
            // - 重做: 富属性的无视
            op.undo = RichED::ExecuteRich;
            op.redo = RichED::UndoRedoIdle;
            break;
        case Op_InsertText:
            // 删除文本
            // - 撤销: 文本移除
            // - 重做: 文本添加
            op.undo = RichED::RollbackText;
            op.redo = RichED::ExecuteText;
            break;
        }
    }
}