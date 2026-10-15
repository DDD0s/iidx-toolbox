#include "std_include.hpp"

#include <stdexcept>

#include "module.hpp"

namespace tb::module
{
	namespace
	{
		HMODULE g_handle = nullptr;

		struct pattern_t
		{
			std::vector<uint8_t> bytes;
			std::vector<bool> exact;
		};

		pattern_t parse(const std::string& text)
		{
			pattern_t out;
			int nibble = -1;
			for (const char c : text)
			{
				if (c == ' ')
					continue;
				if (c == '?')
				{
					out.bytes.push_back(0);
					out.exact.push_back(false);
					continue;
				}
				int v;
				if (c >= '0' && c <= '9') v = c - '0';
				else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
				else throw std::runtime_error("bad pattern");
				if (nibble < 0)
					nibble = v;
				else
				{
					out.bytes.push_back(static_cast<uint8_t>((nibble << 4) | v));
					out.exact.push_back(true);
					nibble = -1;
				}
			}
			if (nibble >= 0)
				throw std::runtime_error("bad pattern");
			while (!out.exact.empty() && !out.exact.back())
			{
				out.exact.pop_back();
				out.bytes.pop_back();
			}
			return out;
		}

		bool readable(DWORD protect)
		{
			if (protect & (PAGE_GUARD | PAGE_NOACCESS))
				return false;
			return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
				PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
		}
	}

	void set(HMODULE handle)
	{
		g_handle = handle;
	}

	HMODULE handle()
	{
		return g_handle;
	}

	std::vector<uint8_t*> find_all(const std::string& text)
	{
		std::vector<uint8_t*> result;
		if (!g_handle)
			return result;
		const auto pattern = parse(text);
		const size_t n = pattern.bytes.size();
		if (!n || !pattern.exact[0])
			return result;

		auto* base = reinterpret_cast<uint8_t*>(g_handle);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
		const size_t image_size = nt->OptionalHeader.SizeOfImage;

		size_t offset = 0;
		while (offset < image_size)
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery(base + offset, &mbi, sizeof(mbi)))
				break;
			const size_t end = std::min<size_t>(image_size,
				static_cast<size_t>(static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - base));
			if (mbi.State == MEM_COMMIT && readable(mbi.Protect) && end >= n)
			{
				for (size_t i = offset; i + n <= end;)
				{
					const auto* hit = static_cast<const uint8_t*>(std::memchr(base + i, pattern.bytes[0], end - n + 1 - i));
					if (!hit)
						break;
					i = static_cast<size_t>(hit - base);
					size_t j = 1;
					while (j < n && (!pattern.exact[j] || base[i + j] == pattern.bytes[j]))
						++j;
					if (j == n)
						result.push_back(base + i);
					++i;
				}
			}
			offset = end;
		}
		return result;
	}

	uint8_t* find_unique(const std::string& pattern)
	{
		const auto hits = find_all(pattern);
		return hits.size() == 1 ? hits[0] : nullptr;
	}

	uint8_t* rel32(uint8_t* at, int extra)
	{
		int32_t disp;
		std::memcpy(&disp, at, 4);
		return at + 4 + extra + disp;
	}
}
