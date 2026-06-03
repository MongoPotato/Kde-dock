#pragma once

// TempConfigDir — RAII helper for test isolation.
// Creates a temporary directory, redirects QStandardPaths to point there,
// and cleans up on destruction.
// Use this in every test that touches the filesystem so tests never write
// to the real ~/.config/kdock directory.
//
// Usage:
//   TempConfigDir tmp;    // sets up isolation in constructor
//   // ... run test code ...
//   // destructor cleans up automatically

#include <QStandardPaths>
#include <QTemporaryDir>

class TempConfigDir {
public:
    TempConfigDir()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_dir.setAutoRemove(true);
    }

    ~TempConfigDir()
    {
        QStandardPaths::setTestModeEnabled(false);
    }

    QString path() const { return m_dir.path(); }
    bool isValid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
};
