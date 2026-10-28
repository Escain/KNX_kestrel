// Bit-field DPTs (KNX 3/7/2 §3.1-3.3, §3.17-3.18):
//   1   B1        boolean, short form
//   2   B2        control + value, short form
//   3   B1U3      dimming / blinds step, short form
//   17  r2U6      scene number
//   18  B1r1U6    scene control (learn bit + scene number)

#include "Detail.hpp"

#include <array>

namespace kestrel::dpt::detail
{

namespace
{

// ----- DPT 1 -------------------------------------------------------------------------------

struct BoolLabels
{
	std::uint16_t sub;
	std::string_view off; // text for 0
	std::string_view on;  // text for 1
};

// Encoding texts of the 1.xxx subtypes (KNX 3/7/2 §3.1). sub 0 (unspecified) reads as switch.
constexpr std::array kBoolLabels = {
	BoolLabels{0, "Off", "On"},
	BoolLabels{1, "Off", "On"},
	BoolLabels{2, "False", "True"},
	BoolLabels{3, "Disable", "Enable"},
	BoolLabels{4, "No ramp", "Ramp"},
	BoolLabels{5, "No alarm", "Alarm"},
	BoolLabels{6, "Low", "High"},
	BoolLabels{7, "Decrease", "Increase"},
	BoolLabels{8, "Up", "Down"},
	BoolLabels{9, "Open", "Close"},
	BoolLabels{10, "Stop", "Start"},
	BoolLabels{11, "Inactive", "Active"},
	BoolLabels{12, "Not inverted", "Inverted"},
	BoolLabels{13, "Start/stop", "Cyclically"},
	BoolLabels{14, "Fixed", "Calculated"},
	BoolLabels{15, "No action", "Reset"},
	BoolLabels{16, "No action", "Acknowledge"},
	BoolLabels{17, "Trigger", "Trigger"},
	BoolLabels{18, "Not occupied", "Occupied"},
	BoolLabels{19, "Closed", "Open"},
	BoolLabels{21, "OR", "AND"},
	BoolLabels{22, "Scene A", "Scene B"},
	BoolLabels{23, "Up/Down only", "Up/Down + step-stop"},
	BoolLabels{24, "Day", "Night"},
};

const BoolLabels* boolLabels(std::uint16_t sub)
{
	for (const BoolLabels& labels : kBoolLabels)
	{
		if (labels.sub == sub)
		{
			return &labels;
		}
	}
	return nullptr;
}

std::string boolText(std::uint16_t sub, bool value)
{
	if (const BoolLabels* labels = boolLabels(sub))
	{
		return std::string(value ? labels->on : labels->off);
	}
	return value ? "1" : "0";
}

/// Generic tokens first, then the subtype's own labels (on-label first, so "Trigger" -> true).
std::optional<bool> parseBoolText(std::uint16_t sub, std::string_view text)
{
	text = trim(text);
	for (std::string_view token : {"1", "on", "true"})
	{
		if (equalsNoCase(text, token))
		{
			return true;
		}
	}
	for (std::string_view token : {"0", "off", "false"})
	{
		if (equalsNoCase(text, token))
		{
			return false;
		}
	}
	if (const BoolLabels* labels = boolLabels(sub))
	{
		if (equalsNoCase(text, labels->on))
		{
			return true;
		}
		if (equalsNoCase(text, labels->off))
		{
			return false;
		}
	}
	return std::nullopt;
}

Result<Bytes> encodeDpt1(DptId id, const DptValue& value)
{
	auto b = toBool(value);
	if (!b)
	{
		return invalid(id, b.error().message);
	}
	return Bytes{static_cast<std::uint8_t>(*b ? 1 : 0)};
}

Result<DptValue> decodeDpt1(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{(bytes[0] & 0x01) != 0};
}

std::optional<std::string> formatDpt1(DptId id, const DptValue& value)
{
	auto b = toBool(value);
	if (!b)
	{
		return std::nullopt;
	}
	return boolText(id.sub, *b);
}

Result<DptValue> parseDpt1(DptId id, std::string_view text)
{
	if (auto b = parseBoolText(id.sub, text))
	{
		return DptValue{*b};
	}
	return invalid(id, "cannot parse '" + std::string(text) + "' as boolean");
}

// ----- DPT 2: bit1 = c (control), bit0 = v (value as in the matching 1.xxx) ----------------

Result<Bytes> encodeDpt2(DptId id, const DptValue& value)
{
	auto raw = toUnsigned(value, 3);
	if (!raw)
	{
		return invalid(id, raw.error().message);
	}
	return Bytes{static_cast<std::uint8_t>(*raw)};
}

Result<DptValue> decodeDpt2(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{std::uint64_t{bytes[0] & 0x03u}};
}

/// "Control, On" / "No control, Off" -- the value part uses the labels of 1.<same sub>.
std::optional<std::string> formatDpt2(DptId id, const DptValue& value)
{
	auto raw = toUnsigned(value, 3);
	if (!raw)
	{
		return std::nullopt;
	}
	return std::string((*raw & 0x02) ? "Control, " : "No control, ") + boolText(id.sub, (*raw & 0x01) != 0);
}

Result<DptValue> parseDpt2(DptId id, std::string_view text)
{
	text = trim(text);
	if (auto raw = parseUnsigned(text))
	{
		return DptValue{*raw};
	}
	std::uint64_t control = 0;
	if (startsWithNoCase(text, "no control"))
	{
		text.remove_prefix(10);
	}
	else if (startsWithNoCase(text, "control"))
	{
		text.remove_prefix(7);
		control = 2;
	}
	else
	{
		return invalid(id, "cannot parse '" + std::string(text) + "' (expected 0..3 or 'Control, On')");
	}
	text = trim(text);
	if (!text.empty() && (text.front() == ',' || text.front() == ':'))
	{
		text.remove_prefix(1);
	}
	if (auto b = parseBoolText(id.sub, text))
	{
		return DptValue{control | (*b ? 1u : 0u)};
	}
	return invalid(id, "cannot parse value part '" + std::string(text) + "'");
}

// ----- DPT 3: bit3 = c (direction), bits2..0 = step code (0 = break) ----------------------

/// Direction words per subtype: 3.008 (blinds) uses Up(0)/Down(1), everything else
/// reads like 3.007 (dimming) Decrease(0)/Increase(1).
std::string_view directionWord(std::uint16_t sub, bool c)
{
	if (sub == 8)
	{
		return c ? "Down" : "Up";
	}
	return c ? "Increase" : "Decrease";
}

Result<Bytes> encodeDpt3(DptId id, const DptValue& value)
{
	auto raw = toUnsigned(value, 15);
	if (!raw)
	{
		return invalid(id, raw.error().message);
	}
	return Bytes{static_cast<std::uint8_t>(*raw)};
}

Result<DptValue> decodeDpt3(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{std::uint64_t{bytes[0] & 0x0Fu}};
}

/// "Increase 3", "Up 1", "Stop" (step code 0 is a break whatever the direction bit).
std::optional<std::string> formatDpt3(DptId id, const DptValue& value)
{
	auto raw = toUnsigned(value, 15);
	if (!raw)
	{
		return std::nullopt;
	}
	const std::uint64_t step = *raw & 0x07;
	if (step == 0)
	{
		return "Stop";
	}
	return std::string(directionWord(id.sub, (*raw & 0x08) != 0)) + " " + std::to_string(step);
}

Result<DptValue> parseDpt3(DptId id, std::string_view text)
{
	text = trim(text);
	if (auto raw = parseUnsigned(text))
	{
		return DptValue{*raw};
	}
	if (equalsNoCase(text, "stop") || equalsNoCase(text, "break"))
	{
		return DptValue{std::uint64_t{0}};
	}
	struct Word
	{
		std::string_view text;
		std::uint64_t c;
	};
	for (const Word& word : {Word{"increase", 8}, Word{"decrease", 0}, Word{"down", 8}, Word{"up", 0}})
	{
		if (startsWithNoCase(text, word.text))
		{
			auto step = parseUnsigned(text.substr(word.text.size()));
			if (step && *step <= 7)
			{
				return DptValue{word.c | *step};
			}
			break;
		}
	}
	return invalid(id, "cannot parse '" + std::string(text) + "' (expected 0..15, 'Increase 3', 'Up 1', 'Stop')");
}

// ----- DPT 17: r2U6 scene number -----------------------------------------------------------

Result<Bytes> encodeDpt17(DptId id, const DptValue& value)
{
	auto scene = toUnsigned(value, 63);
	if (!scene)
	{
		return invalid(id, scene.error().message);
	}
	return Bytes{static_cast<std::uint8_t>(*scene)};
}

Result<DptValue> decodeDpt17(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{std::uint64_t{bytes[0] & 0x3Fu}}; // reserved bits ignored
}

// ----- DPT 18: B1r1U6 scene control --------------------------------------------------------

constexpr std::uint64_t kLearnBit = 0x80;
constexpr std::uint64_t kReservedBit18 = 0x40;

Result<Bytes> encodeDpt18(DptId id, const DptValue& value)
{
	auto raw = toUnsigned(value, 0xFF);
	if (!raw)
	{
		return invalid(id, raw.error().message);
	}
	if (*raw & kReservedBit18)
	{
		return invalid(id, "bit 6 is reserved and must be 0");
	}
	return Bytes{static_cast<std::uint8_t>(*raw)};
}

Result<DptValue> decodeDpt18(DptId id, ByteView bytes)
{
	KESTREL_CHECK(checkSize(id, bytes, 1));
	return DptValue{std::uint64_t{bytes[0] & ~kReservedBit18 & 0xFFu}};
}

/// "Activate 5" / "Learn 5".
std::optional<std::string> formatDpt18(DptId, const DptValue& value)
{
	auto raw = toUnsigned(value, 0xFF);
	if (!raw)
	{
		return std::nullopt;
	}
	return std::string((*raw & kLearnBit) ? "Learn " : "Activate ") + std::to_string(*raw & 0x3F);
}

Result<DptValue> parseDpt18(DptId id, std::string_view text)
{
	text = trim(text);
	if (auto raw = parseUnsigned(text))
	{
		return DptValue{*raw};
	}
	std::uint64_t learn = 0;
	if (startsWithNoCase(text, "activate"))
	{
		text.remove_prefix(8);
	}
	else if (startsWithNoCase(text, "learn"))
	{
		text.remove_prefix(5);
		learn = kLearnBit;
	}
	else
	{
		return invalid(id, "cannot parse '" + std::string(text) + "' (expected 0..255, 'Activate 5', 'Learn 5')");
	}
	auto scene = parseUnsigned(text);
	if (!scene || *scene > 63)
	{
		return invalid(id, "scene number must be 0..63");
	}
	return DptValue{learn | *scene};
}

constexpr std::array kCodecs = {
	Codec{1, 1, Codec::Kind::Bool, encodeDpt1, decodeDpt1, formatDpt1, parseDpt1},
	Codec{2, 2, Codec::Kind::Unsigned, encodeDpt2, decodeDpt2, formatDpt2, parseDpt2},
	Codec{3, 4, Codec::Kind::Unsigned, encodeDpt3, decodeDpt3, formatDpt3, parseDpt3},
	Codec{17, 8, Codec::Kind::Unsigned, encodeDpt17, decodeDpt17, nullptr, nullptr},
	Codec{18, 8, Codec::Kind::Unsigned, encodeDpt18, decodeDpt18, formatDpt18, parseDpt18},
};

} // namespace

const Codec* findBitCodec(std::uint16_t main)
{
	for (const Codec& codec : kCodecs)
	{
		if (codec.main == main)
		{
			return &codec;
		}
	}
	return nullptr;
}

} // namespace kestrel::dpt::detail
