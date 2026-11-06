#include "pefile.h"

#include <QFile>

#include <cstring>

namespace {

constexpr quint16 kMachineX86 = 0x014c;
constexpr quint16 kMachineX64 = 0x8664;
constexpr quint16 kMachineArm64 = 0xAA64;

template <typename T> bool readAt(const QByteArray& data, qint64 offset, T* out)
{
    if (offset < 0 || offset + static_cast<qint64>(sizeof(T)) > data.size())
        return false;
    std::memcpy(out, data.constData() + offset, sizeof(T)); // PE files are little endian, like x86/ARM
    return true;
}

struct Section {
    quint32 virtualAddress;
    quint32 virtualSize;
    quint32 rawPointer;
    quint32 rawSize;
};

qint64 rvaToOffset(const QList<Section>& sections, quint32 rva)
{
    for (const Section& s : sections) {
        const quint32 size = qMax(s.virtualSize, s.rawSize);
        if (rva >= s.virtualAddress && rva < s.virtualAddress + size)
            return static_cast<qint64>(rva - s.virtualAddress) + s.rawPointer;
    }
    return -1;
}

} // namespace

bool PeFileInfo::isNppPlugin() const
{
    for (const char* name : {"setInfo", "getName", "getFuncsArray", "beNotified", "messageProc"}) {
        if (!exports.contains(QLatin1String(name)))
            return false;
    }
    return true;
}

bool PeFileInfo::matchesCurrentProcess() const
{
#if defined(_M_ARM64) || defined(__aarch64__)
    return machine == kMachineArm64;
#elif defined(_M_X64) || defined(__x86_64__)
    return machine == kMachineX64;
#else
    return machine == kMachineX86;
#endif
}

QString PeFileInfo::machineName() const
{
    switch (machine) {
    case kMachineX86:
        return QStringLiteral("32-bit (x86)");
    case kMachineX64:
        return QStringLiteral("64-bit (x64)");
    case kMachineArm64:
        return QStringLiteral("ARM64");
    default:
        return QStringLiteral("unknown (0x%1)").arg(machine, 4, 16, QLatin1Char('0'));
    }
}

PeFileInfo PeFileInfo::inspect(const QString& path)
{
    PeFileInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("cannot be read");
        return info;
    }
    // Plugin DLLs are small; cap the read to protect against huge files.
    const QByteArray data = file.read(64 * 1024 * 1024);

    quint16 mz = 0;
    quint32 peOffset = 0;
    if (!readAt(data, 0, &mz) || mz != 0x5A4D || !readAt(data, 0x3C, &peOffset)) {
        info.error = QStringLiteral("is not a Windows DLL");
        return info;
    }
    quint32 signature = 0;
    if (!readAt(data, peOffset, &signature) || signature != 0x00004550) {
        info.error = QStringLiteral("is not a Windows DLL");
        return info;
    }

    const qint64 coff = static_cast<qint64>(peOffset) + 4;
    quint16 sectionCount = 0;
    quint16 optionalSize = 0;
    readAt(data, coff, &info.machine);
    readAt(data, coff + 2, &sectionCount);
    readAt(data, coff + 16, &optionalSize);

    const qint64 optional = coff + 20;
    quint16 magic = 0;
    if (!readAt(data, optional, &magic) || (magic != 0x10b && magic != 0x20b)) {
        info.error = QStringLiteral("has an invalid PE header");
        return info;
    }
    const bool pe32plus = magic == 0x20b;
    const qint64 dataDirectories = optional + (pe32plus ? 112 : 96);
    quint32 exportRva = 0;
    quint32 exportSize = 0;
    readAt(data, dataDirectories, &exportRva);
    readAt(data, dataDirectories + 4, &exportSize);

    QList<Section> sections;
    const qint64 sectionTable = optional + optionalSize;
    for (int i = 0; i < sectionCount; ++i) {
        const qint64 base = sectionTable + i * 40;
        Section s{};
        if (!readAt(data, base + 8, &s.virtualSize) || !readAt(data, base + 12, &s.virtualAddress) ||
            !readAt(data, base + 16, &s.rawSize) || !readAt(data, base + 20, &s.rawPointer)) {
            break;
        }
        sections.append(s);
    }

    info.valid = true;
    if (exportRva == 0 || exportSize == 0)
        return info; // valid DLL without exports

    const qint64 exportDir = rvaToOffset(sections, exportRva);
    quint32 nameCount = 0;
    quint32 namesRva = 0;
    if (exportDir < 0 || !readAt(data, exportDir + 24, &nameCount) || !readAt(data, exportDir + 32, &namesRva))
        return info;
    const qint64 names = rvaToOffset(sections, namesRva);
    if (names < 0)
        return info;
    nameCount = qMin<quint32>(nameCount, 100000);
    for (quint32 i = 0; i < nameCount; ++i) {
        quint32 nameRva = 0;
        if (!readAt(data, names + i * 4, &nameRva))
            break;
        const qint64 nameOffset = rvaToOffset(sections, nameRva);
        if (nameOffset < 0 || nameOffset >= data.size())
            continue;
        const char* start = data.constData() + nameOffset;
        const qint64 maxLen = qMin<qint64>(data.size() - nameOffset, 512);
        const qint64 len = static_cast<qint64>(qstrnlen(start, static_cast<uint>(maxLen)));
        info.exports.append(QString::fromLatin1(start, len));
    }
    return info;
}
