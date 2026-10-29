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
	// 1. Every parameter at its own default, active or not, except members of a union other than its
	//    DefaultUnionParameter (they share its memory). A program converted from pre-ETS4 data has its default
	//    image in the <Data> already: only parameters with LegacyPatchAlways="true" are written while inactive.
	//    (A Merten MTN670804 sent nothing with the defaults of hidden locking parameters written, and 0x3f instead of
	//    1 with an 8-bit union member's default over its 1-bit value.)
	const bool legacy = attributeOf(app.attributes, u"ConvertedFromPreEts4Data"_s) == u"true"
		|| attributeOf(app.attributes, u"PreEts4Style"_s) == u"true";
	for (const catalog::Parameter& parameter : app.parameters)
	{
		const QString patchAlways = attributeOf(parameter.attributes, u"LegacyPatchAlways"_s);
		if (legacy ? patchAlways != u"true"
				   : (parameter.unionMember && !parameter.unionMember->isDefault) || patchAlways == u"false")
		{
			continue;
		}
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

namespace
{

constexpr unsigned kValueSegmentBit = 0x20; ///< System 2 config bit 5: which segment the value pointer is in

std::vector<std::pair<std::uint16_t, unsigned>> associations(const Links& links,
	const std::vector<std::uint16_t>& addressTable)
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
	std::vector<std::pair<std::uint16_t, unsigned>> out;
	out.reserve(entries.size());
	for (const Entry& entry : entries)
	{
		out.emplace_back(entry.index, entry.object);
	}
	return out;
}


/// The links of active objects (object numbers that some active ComObjectRef uses); the others are reported.
Links activeLinks(const catalog::ApplicationProgram& app, const Evaluation& evaluation, const Links& links,
	bool objectZeroAllowed, QStringList& warnings)
{
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
	for (const auto& [object, addresses] : links)
	{
		if (object == 0 && !objectZeroAllowed)
		{
			warnings.append(u"group object 0 cannot be loaded on System B (objects are numbered from 1)"_s);
			continue;
		}
		if (!activeNumbers.contains(object))
		{
			warnings.append(u"group object %1 is not active with these parameters; its links are not loaded"_s.arg(object));
			continue;
		}
		active[object] = addresses;
	}
	return active;
}

Result<Bytes*> placement(std::map<QString, Bytes>& segments, const std::optional<catalog::TablePlacement>& table,
	const char* what, std::size_t size)
{
	if (!table)
	{
		return makeError(ErrorCode::InvalidArgument, std::string("the application has no ") + what);
	}
	const auto segment = segments.find(table->codeSegment);
	if (segment == segments.end() || table->offset + size > segment->second.size())
	{
		return makeError(ErrorCode::InvalidArgument, std::string(what) + " does not fit its code segment");
	}
	return &segment->second;
}

} // namespace

Bytes buildAssociationTable(const Links& links, const std::vector<std::uint16_t>& addressTable)
{
	const auto entries = associations(links, addressTable);
	Bytes out;
	putU16(out, static_cast<std::uint16_t>(entries.size()));
	for (const auto& [index, object] : entries)
	{
		putU16(out, index);
		putU16(out, static_cast<std::uint16_t>(object));
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
	const Links active = activeLinks(app, evaluation, links, false, result.warnings);
	std::set<std::uint16_t> addressSet;
	for (const auto& [object, addresses] : active)
	{
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


Result<System2Images> buildSystem2(const catalog::ApplicationProgram& app, const ParameterValues& values,
	const Links& links, std::uint16_t individualAddress)
{
	System2Images result;
	KESTREL_TRY(const Evaluation evaluation, evaluate(app, values));
	for (const QString& warning : evaluation.warnings)
	{
		result.warnings.append(warning);
	}
	const Links active = activeLinks(app, evaluation, links, true, result.warnings);
	std::set<std::uint16_t> addressSet;
	for (const auto& [object, addresses] : active)
	{
		addressSet.insert(addresses.begin(), addresses.end());
	}
	const std::vector<std::uint16_t> addresses(addressSet.begin(), addressSet.end());
	if (addresses.size() > 254 || (app.addressTable && app.addressTable->maxEntries
			&& addresses.size() > *app.addressTable->maxEntries))
	{
		return makeError(ErrorCode::InvalidArgument, "more group addresses than the device's address table holds");
	}
	const auto entries = associations(active, addresses);
	if (entries.size() > 255 || (app.associationTable && app.associationTable->maxEntries
			&& entries.size() > *app.associationTable->maxEntries))
	{
		return makeError(ErrorCode::InvalidArgument, "more links than the device's association table holds");
	}

	// Every absolute segment at its <Data>, with the parameters written over it.
	KESTREL_TRY(auto segments, buildParameterSegments(app, values, evaluation, result.warnings));

	// Address table: count (with the device's own address), own address, group addresses ascending.
	{
		const std::size_t size = 3 + 2 * addresses.size();
		KESTREL_TRY(Bytes* segment, placement(segments, app.addressTable, "address table", size));
		Bytes table;
		table.push_back(static_cast<std::uint8_t>(addresses.size() + 1));
		putU16(table, individualAddress);
		for (const std::uint16_t address : addresses)
		{
			putU16(table, address);
		}
		std::copy(table.begin(), table.end(), segment->begin() + app.addressTable->offset);
	}
	// Association table: count, then (connection number = address table index, own address 0; object number).
	{
		const std::size_t size = 1 + 2 * entries.size();
		KESTREL_TRY(Bytes* segment, placement(segments, app.associationTable, "association table", size));
		Bytes table{static_cast<std::uint8_t>(entries.size())};
		for (const auto& [index, object] : entries)
		{
			table.push_back(static_cast<std::uint8_t>(index));
			table.push_back(static_cast<std::uint8_t>(object));
		}
		std::copy(table.begin(), table.end(), segment->begin() + app.associationTable->offset);
	}
	// Group object table: count, RAM flags pointer, then per object value pointer, config, type. The pointers
	// stay as the segment's <Data> has them; config and type follow the active ComObjectRef.
	{
		KESTREL_TRY(Bytes* segment, placement(segments, app.comObjectTable, "group object table", 2));
		const std::size_t base = app.comObjectTable->offset;
		const unsigned count = (*segment)[base];
		if (base + 2 + 3 * std::size_t{count} > segment->size())
		{
			return makeError(ErrorCode::InvalidArgument, "group object table does not fit its code segment");
		}
		std::set<unsigned> done;
		for (const QString& refId : evaluation.comObjectRefs)
		{
			const catalog::ComObjectRef* ref = app.findComObjectRef(refId);
			const catalog::ComObject* object = ref ? app.findComObject(ref->refId) : nullptr;
			if (!object || object->number >= count || !done.insert(object->number).second)
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
			const std::size_t entry = base + 2 + 3 * std::size_t{object->number};
			const bool linked = active.contains(object->number) && !active.at(object->number).empty();
			unsigned config = (*segment)[entry + 1] & kValueSegmentBit;
			config |= ref->update.value_or(object->flags.update) ? 0x80u : 0u;
			config |= ref->transmit.value_or(object->flags.transmit) ? 0x40u : 0u;
			config |= ref->write.value_or(object->flags.write) ? 0x10u : 0u;
			config |= ref->read.value_or(object->flags.read) ? 0x08u : 0u;
			config |= ref->communication.value_or(object->flags.communication) && linked ? 0x04u : 0u;
			QString priority = attributeOf(ref->attributes, u"Priority"_s);
			if (priority.isEmpty())
			{
				priority = attributeOf(object->attributes, u"Priority"_s);
			}
			config |= priorityCode(priority);
			(*segment)[entry + 1] = static_cast<std::uint8_t>(config);
			(*segment)[entry + 2] = *type;
		}
		for (unsigned number = 0; number < count; ++number)
		{
			if (!done.contains(number))
			{
				// Inactive: no communication, value location and type as the <Data> has them.
				const std::size_t entry = base + 2 + 3 * std::size_t{number};
				(*segment)[entry + 1] = static_cast<std::uint8_t>(((*segment)[entry + 1] & kValueSegmentBit) | 0x03u);
			}
		}
	}
	for (const catalog::AbsoluteSegment& segment : app.absoluteSegments)
	{
		if (segment.address > 0xFFFF)
		{
			return makeError(ErrorCode::Unsupported, "absolute segment beyond 64 KiB");
		}
		result.segments[static_cast<std::uint16_t>(segment.address)] = std::move(segments[segment.id]);
		if (segment.mask)
		{
			result.masks[static_cast<std::uint16_t>(segment.address)] = toBytes(*segment.mask);
		}
	}
	return result;
}

QString originalManufacturer(const QString& manufacturerId, const catalog::ApplicationProgram& app)
{
	// An OEM program carries its original manufacturer, and the device reports that one (a Merten MTN670804
	// in Schneider's M-0064 file reports 000C).
	const QString original = attributeOf(app.attributes, u"OriginalManufacturer"_s);
	return original.isEmpty() ? manufacturerId : original;
}

Bytes programVersion(const QString& manufacturerId, const catalog::ApplicationProgram& app)
{
	const uint manufacturer = originalManufacturer(manufacturerId, app).mid(2).toUInt(nullptr, 16);
	return {static_cast<std::uint8_t>(manufacturer >> 8), static_cast<std::uint8_t>(manufacturer),
		static_cast<std::uint8_t>(app.applicationNumber >> 8), static_cast<std::uint8_t>(app.applicationNumber),
		static_cast<std::uint8_t>(app.applicationVersion)};
}

} // namespace kestrel::appconfig
