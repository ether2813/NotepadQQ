#include "include/encodingdetector.h"

#include <QString>

#if defined(NQQ_HAVE_UCHARDET)
#include <uchardet.h>
#elif defined(Q_OS_WIN)
#include <mlang.h>
#include <windows.h>
#endif

namespace EncodingDetector {

bool isValidUtf8(const char* data, qsizetype size)
{
    const auto* p = reinterpret_cast<const unsigned char*>(data);
    qsizetype i = 0;
    while (i < size) {
        const unsigned char c = p[i];
        int len = 0;
        if (c < 0x80)
            len = 1;
        else if (c >= 0xC2 && c <= 0xDF)
            len = 2;
        else if (c >= 0xE0 && c <= 0xEF)
            len = 3;
        else if (c >= 0xF0 && c <= 0xF4)
            len = 4;
        else
            return false;
        if (i + len > size) {
            // A multi-byte sequence cut by the detection window is not an error.
            return true;
        }
        for (int k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80)
                return false;
        }
        i += len;
    }
    return true;
}

QByteArray detect(const char* data, qsizetype size)
{
#if defined(NQQ_HAVE_UCHARDET)
    QByteArray result;
    uchardet_t detector = uchardet_new();
    if (uchardet_handle_data(detector, data, static_cast<size_t>(size)) == 0) {
        uchardet_data_end(detector);
        result = QByteArray(uchardet_get_charset(detector));
    }
    uchardet_delete(detector);
    return result;
#elif defined(Q_OS_WIN)
    if (isValidUtf8(data, size))
        return QByteArrayLiteral("UTF-8");

    QByteArray result;
    IMultiLanguage2* ml = nullptr;
    const HRESULT hr = CoCreateInstance(
        CLSID_CMultiLanguage, nullptr, CLSCTX_INPROC_SERVER, IID_IMultiLanguage2, reinterpret_cast<void**>(&ml));
    if (FAILED(hr) || !ml)
        return result;

    INT srcSize = static_cast<INT>(qMin<qsizetype>(size, 0x7FFFFFFF));
    DetectEncodingInfo info[8];
    INT count = 8;
    if (SUCCEEDED(ml->DetectInputCodepage(MLDETECTCP_NONE, 0, const_cast<CHAR*>(data), &srcSize, info, &count)) &&
        count > 0) {
        int best = 0;
        for (int i = 1; i < count; ++i) {
            if (info[i].nConfidence > info[best].nConfidence)
                best = i;
        }
        MIMECPINFO cpInfo;
        if (SUCCEEDED(ml->GetCodePageInfo(info[best].nCodePage, info[best].nLangID, &cpInfo)))
            result = QString::fromWCharArray(cpInfo.wszWebCharset).toLatin1();
        if (result.isEmpty())
            result = "windows-" + QByteArray::number(static_cast<int>(info[best].nCodePage));
    }
    ml->Release();
    return result;
#else
    if (isValidUtf8(data, size))
        return QByteArrayLiteral("UTF-8");
    return QByteArrayLiteral("ISO-8859-1");
#endif
}

} // namespace EncodingDetector
