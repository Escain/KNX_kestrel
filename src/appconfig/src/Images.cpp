#include "kestrel/appconfig/Images.hpp"

#include <algorithm>
#include <set>

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;

namespace
{

Bytes toBytes(const QByteArray& data)
{
	return Bytes(data.begin(), data.end());
}

/// Initial content of every code segment: its <Data>, zero-filled to its size.
std::map<QString, Bytes> initialSegments(const catalog::ApplicationProgram& app)
{
	std::map<QString, Bytes> segments;
	for (const catalog::RelativeSegment& segment : app.relativeSegments)
	{
		Bytes bytes = segment.data ? toBytes(*segment.data) : Bytes{};
		bytes.resize(std::max<std::size_t>(bytes.size(), segment.size), 0);
		segments[segment.id] = std::move(bytes);
	}
	for (const catalog::AbsoluteSegment& segment : app.absoluteSegments)
	{
		Bytes bytes = segment.data ? toBytes(*segment.data) : Bytes{};
		bytes.resize(std::max<std::size_t>(bytes.size(), segment.size), 0);
		segments[segment.id] = std::move(bytes);
	}
	return segments;
}

void putU16(Bytes& out, std::uint16_t value)
{
	out.push_back(static_cast<std::uint8_t>(value >> 8));
	out.push_back(static_cast<std::uint8_t>(value));
}

/// Group object type code (size) of the System B group object descriptor.
std::optional<std::uint8_t> typeCode(unsigned sizeInBit)
{
	if (sizeInBit >= 1 && sizeInBit <= 7)
	{
		return static_cast<std::uint8_t>(sizeInBit - 1);
	}
	switch (sizeInBit)
	{
	case 8: return 7;
	case 16: return 8;
	case 24: return 9;
	case 32: return 10;
	case 48: return 11;
	case 64: return 12;
	case 80: return 13;
	case 112: return 14;
	default: return std::nullopt;
	}
}

std::uint8_t priorityCode(const QString& priority)
{
	// System 0, normal ("High" in the XML) 1, urgent ("Alert") 2, low 3.
	if (priority == u"Alert")
	{
		return 2;
	}
	if (priority == u"High")
	{
		return 1;
	}
	return 3;
}

QString attributeOf(const catalog::AttributeMap& attributes, const QString& name)
{
	const auto it = attributes.find(name);
	return it == attributes.end() ? QString() : it->second;
}

} // namespace

Result<std::map<QString, Bytes>> buildParameterSegments(const catalog::ApplicationProgram& app,
	const ParameterValues& values, const Evaluation& evaluation, QStringList& warnings)
{
	std::map<QString, Bytes> segments = initialSegments(app);
	auto write = [&](const catalog::Parameter& parameter, const QString& value) -> Status
	{
		const auto location = app.memoryOf(parameter);
		if (!location || value.isEmpty())
		{
			return ok();
		}
		const catalog::ParameterType* type = app.findParameterType(parameter.parameterType);
		const auto segment = segments.find(location->codeSegment);
		if (!type || segment == segments.end())
		{
			warnings.append(u"%1: unknown parameter type or code segment"_s.arg(parameter.id));
			return ok();
		}
		const Status written = writeParameter(segment->second, *type, *location, value);
		if (!written)
		{
			if (written.error().code == ErrorCode::Unsupported)
			{
				warnings.append(u"%1: %2"_s.arg(parameter.id, QString::fromStdString(written.error().message)));
				return ok();
			}
			return makeError(written.error().code, parameter.id.toStdString() + ": " + written.error().message);
		}
		return ok();
	};
	// 1. Every parameter at its own default, active or not.
	for (const catalog::Parameter& parameter : app.parameters)
	{
		KESTREL_CHECK(write(parameter, parameter.value));
	}
	// 2. The active ParameterRefs at their value (user value, ref default, parameter default).
	for (const QString& refId : evaluation.parameterRefs)
	{
		const catalog::ParameterRef* ref = app.findParameterRef(refId);
		const catalog::Parameter* parameter = ref ? app.findParameter(ref->refId) : nullptr;
		if (parameter)
		{
			KESTREL_CHECK(write(*parameter, valueOf(app, values, refId)));
		}
	}
	return segments;
}

Result<ParameterValues> decodeParameterSegments(const catalog::ApplicationProgram& app,
	const std::map<QString, Bytes>& segments, QStringList& warnings)
{
	ParameterValues values;
	for (int iteration = 0; iteration < 50; ++iteration)
	{
		KESTREL_TRY(const Evaluation evaluation, evaluate(app, values));
		ParameterValues next;
		for (const QString& refId : evaluation.parameterRefs)
		{
			const catalog::ParameterRef* ref = app.findParameterRef(refId);
			const catalog::Parameter* parameter = ref ? app.findParameter(ref->refId) : nullptr;
			const auto location = parameter ? app.memoryOf(*parameter) : std::nullopt;
			const catalog::ParameterType* type = parameter ? app.findParameterType(parameter->parameterType) : nullptr;
			if (!location || !type)
			{
				continue;
			}
			const auto segment = segments.find(location->codeSegment);
			if (segment == segments.end())
			{
				continue;
			}
			auto value = readParameter(segment->second, *type, *location);
			if (!value)
			{
				continue;
			}
			// Keep only values that differ from what the ref shows by default. Compare encoded, as decoding is
			// lossy for floats (60000 reads back as 60006.4 in DPT 9).
			const QString shown = valueOf(app, ParameterValues{}, refId);
			Bytes probe = segment->second;
			if (!writeParameter(probe, *type, *location, shown).has_value()
				|| readParameter(probe, *type, *location).value_or(QString()) != *value)
			{
				next[parameter->id] = *value;
			}
		}
		if (next == values)
		{
			return values;
		}
		values = std::move(next);
	}
	warnings.append(u"parameter values did not settle while decoding"_s);
	return values;
}

Result<Bytes> buildGroupObjectTableSystemB(const catalog::ApplicationProgram& app, const Evaluation& evaluation,
	const Links& links)
{
	unsigned count = 0;
	for (const catalog::ComObject& object : app.comObjects)
	{
		count = std::max(count, object.number);
	}
	if (count > 0xFFFF)
	{
		return makeError(ErrorCode::InvalidArgument, "too many group objects");
	}
	Bytes out;
	putU16(out, static_cast<std::uint16_t>(count));
	out.resize(2 + 2 * std::size_t{count}, 0);
	std::set<unsigned> done;
	for (const QString& refId : evaluation.comObjectRefs)
	{
		const catalog::ComObjectRef* ref = app.findComObjectRef(refId);
		const catalog::ComObject* object = ref ? app.findComObject(ref->refId) : nullptr;
		if (!object || object->number == 0 || object->number > count || !done.insert(object->number).second)
		{
			continue; // the first active ref of a number defines it
		}
		const unsigned size = ref->sizeInBit.value_or(object->sizeInBit);
		const auto type = typeCode(size);
		if (!type)
		{
			return makeError(ErrorCode::Unsupported,
				"group object " + std::to_string(object->number) + ": size of " + std::to_string(size) + " bits");
		}
		const bool linked = links.contains(object->number) && !links.at(object->number).empty();
		unsigned config = 0;
		config |= ref->update.value_or(object->flags.update) ? 0x80u : 0u;
		config |= ref->transmit.value_or(object->flags.transmit) ? 0x40u : 0u;
		config |= ref->readOnInit.value_or(object->flags.readOnInit) ? 0x20u : 0u;
		config |= ref->write.value_or(object->flags.write) ? 0x10u : 0u;
		config |= ref->read.value_or(object->flags.read) ? 0x08u : 0u;
		config |= ref->communication.value_or(object->flags.communication) && linked ? 0x04u : 0u;
		QString priority = attributeOf(ref->attributes, u"Priority"_s);
		if (priority.isEmpty())
		{
			priority = attributeOf(object->attributes, u"Priority"_s);
		}
		config |= priorityCode(priority);
		out[2 * std::size_t{object->number}] = static_cast<std::uint8_t>(config);
		out[2 * std::size_t{object->number} + 1] = *type;
	}
	return out;
}

Bytes buildAddressTable(const std::vector<std::uint16_t>& addresses)
{
	Bytes out;
	putU16(out, static_cast<std::uint16_t>(addresses.size()));
	for (const std::uint16_t address : addresses)
	{
		putU16(out, address);
	}
	return out;
}

Bytes buildAssociationTable(const Links& links, const std::vector<std::uint16_t>& addressTable)
{
	struct Entry
	{
		bool sending;
		unsigned object;
		std::uint16_t index;
	};
	std::vector<Entry> entries;
	for (const auto& [object, addresses] : links)
	{
		for (std::size_t i = 0; i < addresses.size(); ++i)
		{
			const auto it = std::lower_bound(addressTable.begin(), addressTable.end(), addresses[i]);
			if (it == addressTable.end() || *it != addresses[i])
			{
				continue;
			}
			entries.push_back({i == 0, object, static_cast<std::uint16_t>(it - addressTable.begin() + 1)});
		}
	}
	std::stable_sort(entries.begin(), entries.end(),
		[](const Entry& a, const Entry& b)
		{
			if (a.sending != b.sending)
			{
				return a.sending;
			}
			return a.object != b.object ? a.object < b.object : a.index < b.index;
		});
	Bytes out;
	putU16(out, static_cast<std::uint16_t>(entries.size()));
	for (const Entry& entry : entries)
	{
		putU16(out, entry.index);
		putU16(out, static_cast<std::uint16_t>(entry.object));
	}
	return out;
}

Result<SystemBImages> buildSystemB(const catalog::ApplicationProgram& app, const ParameterValues& values,
	const Links& links)
{
	SystemBImages result;
	KESTREL_TRY(const Evaluation evaluation, evaluate(app, values));
	for (const QString& warning : evaluation.warnings)
	{
		result.warnings.append(warning);
	}

	// Links of objects that are not active are dropped (ETS keeps them in the project but does not load them).
	std::set<unsigned> activeNumbers;
	for (const QString& refId : evaluation.comObjectRefs)
	{
		const catalog::ComObjectRef* ref = app.findComObjectRef(refId);
		if (const catalog::ComObject* object = ref ? app.findComObject(ref->refId) : nullptr)
		{
			activeNumbers.insert(object->number);
		}
	}
	Links active;
	std::set<std::uint16_t> addressSet;
	for (const auto& [object, addresses] : links)
	{
		if (object == 0)
		{
			result.warnings.append(u"group object 0 cannot be loaded on System B (objects are numbered from 1)"_s);
			continue;
		}
		if (!activeNumbers.contains(object))
		{
			result.warnings.append(u"group object %1 is not active with these parameters; its links are not loaded"_s
					.arg(object));
			continue;
		}
		active[object] = addresses;
		addressSet.insert(addresses.begin(), addresses.end());
	}
	const std::vector<std::uint16_t> addressTable(addressSet.begin(), addressSet.end());
	if (app.addressTable && app.addressTable->maxEntries && addressTable.size() > *app.addressTable->maxEntries)
	{
		return makeError(ErrorCode::InvalidArgument, "more group addresses than the device's address table holds");
	}

	result.images[1] = buildAddressTable(addressTable);
	result.images[2] = buildAssociationTable(active, addressTable);
	KESTREL_TRY(Bytes groupObjects, buildGroupObjectTableSystemB(app, evaluation, active));
	result.images[3] = std::move(groupObjects);

	KESTREL_TRY(auto segments, buildParameterSegments(app, values, evaluation, result.warnings));
	for (const catalog::RelativeSegment& segment : app.relativeSegments)
	{
		// LSM 1..3 are the tables built above; an application may still declare relative segments for them
		// (their Size only matters for the allocation, see download::resolve).
		if (segment.loadStateMachine <= 3 || segment.loadStateMachine > 0xFF)
		{
			continue;
		}
		result.images[static_cast<std::uint8_t>(segment.loadStateMachine)] = std::move(segments[segment.id]);
	}
	return result;
}

Bytes programVersion(const QString& manufacturerId, const catalog::ApplicationProgram& app)
{
	const uint manufacturer = manufacturerId.mid(2).toUInt(nullptr, 16);
	return {static_cast<std::uint8_t>(manufacturer >> 8), static_cast<std::uint8_t>(manufacturer),
		static_cast<std::uint8_t>(app.applicationNumber >> 8), static_cast<std::uint8_t>(app.applicationNumber),
		static_cast<std::uint8_t>(app.applicationVersion)};
}

} // namespace kestrel::appconfig
