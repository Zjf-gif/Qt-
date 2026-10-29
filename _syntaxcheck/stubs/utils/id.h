#pragma once
// 语法检查用的 Creator SDK 桩：只声明本插件实际用到的 Utils::Id 接口
#include <QString>

namespace Utils {

class Id
{
public:
    Id() = default;
    Id(const char *name) : m_name(QString::fromLatin1(name)) {}
    static Id fromString(const QString &name)
    {
        Id id;
        id.m_name = name;
        return id;
    }

    QString toString() const { return m_name; }
    bool isValid() const { return !m_name.isEmpty(); }
    bool operator==(const Id &other) const { return m_name == other.m_name; }
    bool operator<(const Id &other) const { return m_name < other.m_name; }

private:
    QString m_name;
};

} // namespace Utils
