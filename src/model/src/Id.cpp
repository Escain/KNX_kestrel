#include "kestrel/model/Id.hpp"

#include <QRandomGenerator>

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::model
{

namespace
{

// Crockford base32, lower case: no i, l, o, u.
constexpr char kAlphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
constexpr int kIdChars = 13; // 13 * 5 = 65 bits >= 64; the first char carries the top 4 bits

constexpr std::array kAllKinds = {EntityKind::Project, EntityKind::BuildingPart, EntityKind::Area, EntityKind::Line,
	EntityKind::Device, EntityKind::ComObject, EntityKind::GroupRange, EntityKind::GroupAddress, EntityKind::Link};

int digitValue(char16_t c)
{
	for (int i = 0; i < 32; ++i)
	{
		if (kAlphabet[i] == c)
		{
			return i;
		}
	}
	return -1;
}

} // namespace

const char* toString(EntityKind kind)
{
	switch (kind)
	{
		case EntityKind::Project: return "Project";
		case EntityKind::BuildingPart: return "BuildingPart";
		case EntityKind::Area: return "Area";
		case EntityKind::Line: return "Line";
		case EntityKind::Device: return "Device";
		case EntityKind::ComObject: return "ComObject";
		case EntityKind::GroupRange: return "GroupRange";
		case EntityKind::GroupAddress: return "GroupAddress";
		case EntityKind::Link: return "Link";
	}
	return "?";
}

QString idPrefix(EntityKind kind)
{
	switch (kind)
	{
		case EntityKind::Project: return u"prj"_s;
		case EntityKind::BuildingPart: return u"bp"_s;
		case EntityKind::Area: return u"area"_s;
		case EntityKind::Line: return u"line"_s;
		case EntityKind::Device: return u"dev"_s;
		case EntityKind::ComObject: return u"co"_s;
		case EntityKind::GroupRange: return u"gr"_s;
		case EntityKind::GroupAddress: return u"ga"_s;
		case EntityKind::Link: return u"lnk"_s;
	}
	return u"?"_s;
}

std::ostream& operator<<(std::ostream& os, EntityKind kind)
{
	return os << toString(kind);
}

QString detail::formatId(EntityKind kind, std::uint64_t value)
{
	QString text = idPrefix(kind);
	text.reserve(text.size() + 1 + kIdChars);
	text += u'-';
	for (int i = 0; i < kIdChars; ++i)
	{
		const int shift = 5 * (kIdChars - 1 - i);
		text += QLatin1Char(kAlphabet[(value >> shift) & 31u]);
	}
	return text;
}

std::optional<std::uint64_t> detail::parseId(EntityKind kind, QStringView text)
{
	const QString prefix = idPrefix(kind);
	if (text.size() != prefix.size() + 1 + kIdChars || !text.startsWith(prefix) || text[prefix.size()] != u'-')
	{
		return std::nullopt;
	}
	std::uint64_t value = 0;
	for (qsizetype i = prefix.size() + 1; i < text.size(); ++i)
	{
		const int digit = digitValue(text[i].unicode());
		if (digit < 0 || (i == prefix.size() + 1 && digit > 15))
		{
			return std::nullopt;
		}
		value = (value << 5) | static_cast<std::uint64_t>(digit);
	}
	if (value == 0)
	{
		return std::nullopt;
	}
	return value;
}

QString EntityRef::toString() const
{
	if (kind == EntityKind::Project)
	{
		return u"project"_s;
	}
	return detail::formatId(kind, id);
}

std::optional<EntityRef> EntityRef::parse(QStringView text)
{
	if (text == u"project")
	{
		return project();
	}
	for (const EntityKind kind : kAllKinds)
	{
		if (kind == EntityKind::Project)
		{
			continue;
		}
		if (const auto value = detail::parseId(kind, text))
		{
			return EntityRef(kind, *value);
		}
	}
	return std::nullopt;
}

std::ostream& operator<<(std::ostream& os, const EntityRef& ref)
{
	return os << ref.toString().toStdString();
}

IdGenerator::IdGenerator(std::function<std::uint64_t()> next) : m_next(std::move(next))
{
}

IdGenerator IdGenerator::random()
{
	return IdGenerator(
		[]
		{
			std::uint64_t value = 0;
			while (value == 0)
			{
				value = QRandomGenerator::global()->generate64();
			}
			return value;
		});
}

IdGenerator IdGenerator::sequential(std::uint64_t seed)
{
	return IdGenerator(
		[next = seed]() mutable
		{
			if (next == 0)
			{
				next = 1;
			}
			return next++;
		});
}

std::uint64_t IdGenerator::next()
{
	std::uint64_t value = 0;
	while (value == 0)
	{
		value = m_next();
	}
	return value;
}

} // namespace kestrel::model
