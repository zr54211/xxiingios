#pragma once

#include <cstdint>
#include <string>

#include "types.h"
#include "IMemoryManager.h"

namespace bsz {

// UTF-8 -> UTF-16 (WCHAR_T платформы 1С всегда 2-байтовый).
std::u16string ToU16(const std::string& utf8);

// UTF-16 (буфер платформы) -> UTF-8. len — число символов; npos = до нулевого символа.
std::string ToUtf8(const WCHAR_T* str, size_t len = static_cast<size_t>(-1));

// Копия строки в памяти платформы (через IMemoryManager). Владение передаётся платформе.
bool AllocWString(IMemoryManager* memory, const std::u16string& src, WCHAR_T** dst);

// Экранирование строки для вставки в JSON-значение.
std::string EscapeJson(const std::string& s);

} // namespace bsz
