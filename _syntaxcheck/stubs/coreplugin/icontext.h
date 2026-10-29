#pragma once
#include "utils/id.h"

namespace Core {

// Qt Creator 4.15 里 Context 定义在 coreplugin/icontext.h
class Context
{
public:
    Context() = default;
    explicit Context(Utils::Id id) : m_id(id) {}
    Utils::Id id() const { return m_id; }

private:
    Utils::Id m_id;
};

} // namespace Core
