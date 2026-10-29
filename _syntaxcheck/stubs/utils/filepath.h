#pragma once
#include <QString>

namespace Utils {

class FilePath
{
public:
    FilePath() = default;
    FilePath(const QString &path) : m_path(path) {}
    QString toString() const { return m_path; }
    QString toUserOutput() const { return m_path; }
    bool isEmpty() const { return m_path.isEmpty(); }

private:
    QString m_path;
};

} // namespace Utils
