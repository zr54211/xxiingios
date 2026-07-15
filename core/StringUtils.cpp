#include "StringUtils.h"

namespace bsz {

std::u16string ToU16(const std::string& utf8)
{
	std::u16string result;
	result.reserve(utf8.size());

	size_t i = 0;
	const size_t n = utf8.size();

	while (i < n) {
		const uint8_t b0 = static_cast<uint8_t>(utf8[i]);
		uint32_t cp = 0;
		size_t extra = 0;

		if (b0 < 0x80) {
			cp = b0;
		} else if ((b0 & 0xE0) == 0xC0) {
			cp = b0 & 0x1F;
			extra = 1;
		} else if ((b0 & 0xF0) == 0xE0) {
			cp = b0 & 0x0F;
			extra = 2;
		} else if ((b0 & 0xF8) == 0xF0) {
			cp = b0 & 0x07;
			extra = 3;
		} else {
			// Невалидный байт — заменяем на U+FFFD и идём дальше.
			result.push_back(u'�');
			++i;
			continue;
		}

		bool valid = true;
		for (size_t k = 1; k <= extra; ++k) {
			if (i + k >= n || (static_cast<uint8_t>(utf8[i + k]) & 0xC0) != 0x80) {
				valid = false;
				break;
			}
			cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3F);
		}

		if (!valid) {
			result.push_back(u'�');
			++i;
			continue;
		}

		if (cp >= 0x10000) {
			cp -= 0x10000;
			result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		} else {
			result.push_back(static_cast<char16_t>(cp));
		}

		i += extra + 1;
	}

	return result;
}

std::string ToUtf8(const WCHAR_T* str, size_t len)
{
	std::string result;

	if (!str)
		return result;

	if (len == static_cast<size_t>(-1)) {
		len = 0;
		while (str[len] != 0)
			++len;
	}

	result.reserve(len);

	for (size_t i = 0; i < len; ++i) {
		uint32_t cp = static_cast<uint16_t>(str[i]);

		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
			const uint32_t low = static_cast<uint16_t>(str[i + 1]);

			if (low >= 0xDC00 && low <= 0xDFFF) {
				cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				++i;
			}
		}

		if (cp < 0x80) {
			result.push_back(static_cast<char>(cp));
		} else if (cp < 0x800) {
			result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else if (cp < 0x10000) {
			result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	return result;
}

bool AllocWString(IMemoryManager* memory, const std::u16string& src, WCHAR_T** dst)
{
	if (!memory || !dst)
		return false;

	const size_t bytes = (src.size() + 1) * sizeof(WCHAR_T);

	if (!memory->AllocMemory(reinterpret_cast<void**>(dst), static_cast<unsigned long>(bytes)))
		return false;

	for (size_t i = 0; i < src.size(); ++i)
		(*dst)[i] = static_cast<WCHAR_T>(src[i]);

	(*dst)[src.size()] = 0;
	return true;
}

std::string EscapeJson(const std::string& s)
{
	std::string result;
	result.reserve(s.size() + 8);

	for (const char c : s) {
		switch (c) {
		case '"':  result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\b': result += "\\b";  break;
		case '\f': result += "\\f";  break;
		case '\n': result += "\\n";  break;
		case '\r': result += "\\r";  break;
		case '\t': result += "\\t";  break;
		default:
			if (static_cast<uint8_t>(c) < 0x20) {
				static const char* hex = "0123456789abcdef";
				result += "\\u00";
				result += hex[(c >> 4) & 0x0F];
				result += hex[c & 0x0F];
			} else {
				result += c;
			}
		}
	}

	return result;
}

} // namespace bsz
