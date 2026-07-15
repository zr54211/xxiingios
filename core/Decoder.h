#pragma once

#include <cstdint>
#include <string>

namespace bsz {

// Распознаёт штрихкоды в luminance-буфере (Y-плоскость YUV или 8-битный grayscale).
//
// data   - указатель на буфер width*height байт;
// width  - ширина кадра в пикселях;
// height - высота кадра в пикселях.
//
// Возвращает JSON:
//   {"found":N,"barcodes":[{"format":"QRCode","text":"..."},...]}
// При ошибке параметров: {"found":0,"barcodes":[],"error":"<описание>"}
std::string DecodeLuminance(const uint8_t* data, int width, int height);

// Версия движка zxing-cpp, зашитая в сборку (для диагностики из BSL).
std::string ZXingVersion();

} // namespace bsz
