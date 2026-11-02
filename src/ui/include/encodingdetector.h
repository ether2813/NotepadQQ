#ifndef NQQ_ENCODINGDETECTOR_H
#define NQQ_ENCODINGDETECTOR_H

#include <QByteArray>

/**
 * @brief Guesses the character set of a byte buffer that has no BOM.
 *
 * Uses uchardet when available (Linux/macOS builds), the Windows MLang service
 * (IMultiLanguage2::DetectInputCodepage) on Windows, and otherwise only recognises UTF-8.
 */
namespace EncodingDetector {

/** @return a charset name usable with QTextCodec::codecForName(), or an empty array. */
QByteArray detect(const char* data, qsizetype size);

/** @return true if @p data is valid UTF-8 (pure ASCII included). */
bool isValidUtf8(const char* data, qsizetype size);

} // namespace EncodingDetector

#endif // NQQ_ENCODINGDETECTOR_H
