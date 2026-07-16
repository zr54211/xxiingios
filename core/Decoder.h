#pragma once

#include <cstdint>
#include <string>

namespace bsz {

// Результат распознавания кадра.
struct DecodeResult {
	std::string json; // {"found":N,"barcodes":[{"format","text","points":[[x,y]x4]},...]}
	std::string firstText; // формат+текст первого кода — ключ дедупликации
	int found = 0;
	float points[8] = {}; // нормированные углы первого кода (для маркеров на превью)
};

// Распознаёт штрихкоды в luminance-буфере (Y-плоскость YUV или 8-битный grayscale).
//
// data   - указатель на буфер width*height байт;
// width  - ширина кадра в пикселях;
// height - высота кадра в пикселях.
DecodeResult DecodeLuminanceEx(const uint8_t* data, int width, int height);

// То же, только JSON. При ошибке параметров: {"found":0,...,"error":"<описание>"}
std::string DecodeLuminance(const uint8_t* data, int width, int height);

// Версия движка zxing-cpp, зашитая в сборку (для диагностики из BSL).
std::string ZXingVersion();

} // namespace bsz
