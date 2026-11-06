#ifndef NQQ_PEFILE_H
#define NQQ_PEFILE_H

#include <QString>
#include <QStringList>

/**
 * @brief Minimal, read-only inspection of a Windows PE (DLL/EXE) file.
 *
 * Used to recognise Notepad++ plugins and their architecture *without* loading them (loading a
 * DLL runs its code). Platform independent, so it can be unit-tested anywhere.
 */
struct PeFileInfo {
    bool valid = false;
    QString error;
    quint16 machine = 0; ///< IMAGE_FILE_MACHINE_* (0x8664 x64, 0x14c x86, 0xAA64 ARM64)
    QStringList exports;

    bool isNppPlugin() const;
    /** True if the DLL can be loaded by this process (same architecture). */
    bool matchesCurrentProcess() const;
    QString machineName() const;

    static PeFileInfo inspect(const QString& path);
};

#endif // NQQ_PEFILE_H
