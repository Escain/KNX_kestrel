#include "kestrel/model/Entities.hpp"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace kestrel::model
{

namespace
{

std::string text(const QString& value)
{
	return value.toStdString();
}

std::ostream& quoted(std::ostream& os, const QString& value)
{
	return os << '"' << text(value) << '"';
}

template <class T>
std::ostream& printOptional(std::ostream& os, const std::optional<T>& value)
{
	if (!value)
	{
		return os << "none";
	}
	return os << *value;
}

} // namespace

QString toString(BuildingPartKind kind)
{
	switch (kind)
	{
		case BuildingPartKind::Building: return u"Building"_s;
		case BuildingPartKind::Floor: return u"Floor"_s;
		case BuildingPartKind::Room: return u"Room"_s;
		case BuildingPartKind::Corridor: return u"Corridor"_s;
		case BuildingPartKind::Stairway: return u"Stairway"_s;
		case BuildingPartKind::DistributionBoard: return u"DistributionBoard"_s;
	}
	return u"?"_s;
}

QString toString(Medium medium)
{
	switch (medium)
	{
		case Medium::TP: return u"TP"_s;
		case Medium::IP: return u"IP"_s;
		case Medium::RF: return u"RF"_s;
		case Medium::PL: return u"PL"_s;
	}
	return u"?"_s;
}

QString toString(knx::GroupAddressStyle style)
{
	switch (style)
	{
		case knx::GroupAddressStyle::ThreeLevel: return u"ThreeLevel"_s;
		case knx::GroupAddressStyle::TwoLevel: return u"TwoLevel"_s;
		case knx::GroupAddressStyle::Free: return u"Free"_s;
	}
	return u"?"_s;
}

std::optional<BuildingPartKind> parseBuildingPartKind(QStringView text)
{
	for (const auto kind : {BuildingPartKind::Building, BuildingPartKind::Floor, BuildingPartKind::Room,
			 BuildingPartKind::Corridor, BuildingPartKind::Stairway, BuildingPartKind::DistributionBoard})
	{
		if (toString(kind) == text)
		{
			return kind;
		}
	}
	return std::nullopt;
}

std::optional<Medium> parseMedium(QStringView text)
{
	for (const auto medium : {Medium::TP, Medium::IP, Medium::RF, Medium::PL})
	{
		if (toString(medium) == text)
		{
			return medium;
		}
	}
	return std::nullopt;
}

std::optional<knx::GroupAddressStyle> parseGroupAddressStyle(QStringView text)
{
	for (const auto style :
		{knx::GroupAddressStyle::ThreeLevel, knx::GroupAddressStyle::TwoLevel, knx::GroupAddressStyle::Free})
	{
		if (toString(style) == text)
		{
			return style;
		}
	}
	return std::nullopt;
}

std::ostream& operator<<(std::ostream& os, BuildingPartKind kind)
{
	return os << text(toString(kind));
}

std::ostream& operator<<(std::ostream& os, Medium medium)
{
	return os << text(toString(medium));
}

bool isValidDptRef(QStringView dpt)
{
	static const QRegularExpression kPattern(u"^(DPT-[1-9][0-9]{0,3}|DPST-[1-9][0-9]{0,3}-[1-9][0-9]{0,4})$"_s);
	return dpt.isEmpty() || kPattern.matchView(dpt).hasMatch();
}

QString ComObjectFlags::toString() const
{
	QString letters;
	if (communication)
		letters += u'C';
	if (read)
		letters += u'R';
	if (write)
		letters += u'W';
	if (transmit)
		letters += u'T';
	if (update)
		letters += u'U';
	if (readOnInit)
		letters += u'I';
	return letters;
}

std::optional<ComObjectFlags> ComObjectFlags::parse(QStringView text)
{
	ComObjectFlags flags;
	flags.communication = false;
	for (const QChar c : text)
	{
		bool* flag = nullptr;
		switch (c.unicode())
		{
			case u'C': flag = &flags.communication; break;
			case u'R': flag = &flags.read; break;
			case u'W': flag = &flags.write; break;
			case u'T': flag = &flags.transmit; break;
			case u'U': flag = &flags.update; break;
			case u'I': flag = &flags.readOnInit; break;
			default: return std::nullopt;
		}
		if (*flag)
		{
			return std::nullopt;
		}
		*flag = true;
	}
	return flags;
}

std::ostream& operator<<(std::ostream& os, const ProjectSettings& value)
{
	os << "ProjectSettings{name=";
	quoted(os, value.name) << " gaStyle=" << text(toString(value.gaStyle)) << " lang=" << text(value.contentLanguage);
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const BuildingPart& value)
{
	os << "BuildingPart{" << value.id << " parent=";
	printOptional(os, value.parent) << ' ' << value.kind << " name=";
	quoted(os, value.name) << " description=";
	return quoted(os, value.description) << '}';
}

std::ostream& operator<<(std::ostream& os, const Area& value)
{
	os << "Area{" << value.id << " number=" << value.number << " name=";
	return quoted(os, value.name) << ' ' << value.medium << '}';
}

std::ostream& operator<<(std::ostream& os, const Line& value)
{
	os << "Line{" << value.id << " area=" << value.area << " number=" << value.number << " name=";
	return quoted(os, value.name) << ' ' << value.medium << '}';
}

std::ostream& operator<<(std::ostream& os, const Device& value)
{
	os << "Device{" << value.id << " line=" << value.line << " number=";
	printOptional(os, value.number) << " name=";
	quoted(os, value.name) << " description=";
	quoted(os, value.description) << " place=";
	printOptional(os, value.place) << " product=" << text(value.productRef)
								   << " app=" << text(value.appProgramRef) << " params={";
	bool first = true;
	for (const auto& [ref, paramValue] : value.params)
	{
		os << (first ? "" : ", ") << text(ref) << '=' << text(paramValue);
		first = false;
	}
	return os << "}}";
}

std::ostream& operator<<(std::ostream& os, const ComObjectFlags& value)
{
	return os << text(value.toString());
}

std::ostream& operator<<(std::ostream& os, const ComObject& value)
{
	os << "ComObject{" << value.id << " device=" << value.device << " number=" << value.number << " name=";
	quoted(os, value.name) << " function=";
	quoted(os, value.function) << " size=" << value.sizeInBit << " flags=" << value.flags
							   << " dpt=" << text(value.dpt) << " catalogRef=" << text(value.catalogRef);
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const GroupRange& value)
{
	os << "GroupRange{" << value.id << " parent=";
	printOptional(os, value.parent) << " name=";
	return quoted(os, value.name) << " start=" << value.start << " end=" << value.end << '}';
}

std::ostream& operator<<(std::ostream& os, const GroupAddress& value)
{
	os << "GroupAddress{" << value.id << " range=";
	printOptional(os, value.range) << ' ' << value.address() << " name=";
	quoted(os, value.name) << " dpt=" << text(value.dpt) << " description=";
	return quoted(os, value.description) << '}';
}

std::ostream& operator<<(std::ostream& os, const Link& value)
{
	return os << "Link{" << value.id << ' ' << value.comObject << " -> " << value.groupAddress
			  << (value.sending ? " sending" : "") << '}';
}

std::ostream& operator<<(std::ostream& os, const DeviceState& value)
{
	return os << "DeviceState{iaProgrammed=" << value.iaProgrammed << " serial=" << text(value.serial)
			  << " downloadedHash=" << text(value.downloadedHash) << '}';
}

} // namespace kestrel::model
