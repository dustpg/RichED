#include "ed_undoredo.h"

#include <cstring>
#include <cstdlib>
#include <type_traits>

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
    m_head.next = &m_tail;
    m_tail.prev = &m_head;
    m_cCurrent = 0;
}

// ----------------------------------------------------------------------------
//                                 impl
// ----------------------------------------------------------------------------
