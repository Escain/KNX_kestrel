#include "kestrel/appconfig/Import.hpp"

#include "kestrel/model/Operations.hpp"

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;

namespace
{

std::uint16_t u16(const Bytes& bytes, std::size_t at)
{
	return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
}

} // namespace

Result<Links> decodeSystemBTables(const Bytes& addressTable, const Bytes& associationTable)
{
	if (addressTable.size() < 2 || associationTable.size() < 2)
	{
		return makeError(ErrorCode::ParseError, "table too short");
	}
	const std::size_t addresses = u16(addressTable, 0);
	const std::size_t associations = u16(associationTable, 0);
	if (addressTable.size() < 2 + 2 * addresses || associationTable.size() < 2 + 4 * associations)
	{
		return makeError(ErrorCode::ParseError, "table shorter than its count");
	}
	Links links;
	for (std::size_t i = 0; i < associations; ++i)
	{
		const std::size_t index = u16(associationTable, 2 + 4 * i);
		const unsigned object = u16(associationTable, 4 + 4 * i);
		if (index == 0 || index > addresses)
		{
			return makeError(ErrorCode::ParseError, "association refers to address index " + std::to_string(index));
		}
		auto& list = links[object];
		const std::uint16_t address = u16(addressTable, 2 * index);
		if (std::find(list.begin(), list.end(), address) == list.end())
		{
			list.push_back(address);
		}
	}
	return links;
}

Result<QStringList> importSystemBImages(model::Project& project, model::Id<model::Device> deviceId,
	const catalog::ApplicationProgram& app, const std::map<std::uint8_t, Bytes>& images, const QString& language)
{
	const model::Device* device = project.devices().find(deviceId);
	if (!device)
	{
		return makeError(ErrorCode::NotFound, "no such device");
	}
	if (device->appProgramRef != app.id)
	{
		return makeError(ErrorCode::InvalidArgument, "the device does not use application " + app.id.toStdString());
	}
	for (const std::uint8_t object : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}})
	{
		if (!images.contains(object))
		{
			return makeError(ErrorCode::InvalidArgument, "missing segment image of object " + std::to_string(object));
		}
	}
	const catalog::RelativeSegment* parameterSegment = nullptr;
	for (const catalog::RelativeSegment& segment : app.relativeSegments)
	{
		if (segment.loadStateMachine == 4)
		{
			parameterSegment = &segment;
		}
	}
	if (!parameterSegment)
	{
		return makeError(ErrorCode::Unsupported, "the application has no parameter segment for object 4");
	}

	QStringList warnings;
	KESTREL_TRY(const ParameterValues values,
		decodeParameterSegments(app, {{parameterSegment->id, images.at(4)}}, warnings));
	KESTREL_TRY(const Links links, decodeSystemBTables(images.at(1), images.at(2)));
	KESTREL_TRY(const Evaluation evaluation, evaluate(app, values));

	// Effective com objects per active number (the first active ref of a number).
	const auto effective = app.effectiveComObjects(language);
	std::map<unsigned, const catalog::EffectiveComObject*> byNumber;
	for (const QString& refId : evaluation.comObjectRefs)
	{
		for (const catalog::EffectiveComObject& object : effective)
		{
			if (object.refId == refId && !byNumber.contains(object.number))
			{
				byNumber[object.number] = &object;
			}
		}
	}

	const Status status = project.edit(u"Import device configuration"_s,
		[&](model::Tx&) -> Status
		{
			// Replace what the device had.
			std::vector<model::Id<model::ComObject>> oldObjects;
			for (const model::ComObject& object : project.comObjects().rows())
			{
				if (object.device == deviceId)
				{
					oldObjects.push_back(object.id);
				}
			}
			for (const auto id : oldObjects)
			{
				KESTREL_CHECK(model::removeComObject(project, id));
			}
			const auto oldParams = project.devices().at(deviceId).params;
			for (const auto& [ref, value] : oldParams)
			{
				KESTREL_CHECK(model::setDeviceParam(project, deviceId, ref, std::nullopt));
			}

			for (const auto& [parameterId, value] : values)
			{
				QString refId;
				for (const QString& active : evaluation.parameterRefs)
				{
					const catalog::ParameterRef* ref = app.findParameterRef(active);
					if (ref && ref->refId == parameterId)
					{
						refId = active;
						break;
					}
				}
				if (refId.isEmpty())
				{
					continue;
				}
				KESTREL_CHECK(model::setDeviceParam(project, deviceId, refId, value));
			}

			for (const auto& [number, addresses] : links)
			{
				const auto found = byNumber.find(number);
				if (found == byNumber.end())
				{
					warnings.append(u"object %1 is associated but not active; skipped"_s.arg(number));
					continue;
				}
				const catalog::EffectiveComObject& definition = *found->second;
				model::ComObject object;
				object.number = number;
				object.name = definition.text.isEmpty() ? definition.name : definition.text;
				object.function = definition.functionText;
				object.sizeInBit = definition.sizeInBit == 0 ? 1 : definition.sizeInBit;
				object.flags = model::ComObjectFlags{definition.flags.communication, definition.flags.read,
					definition.flags.write, definition.flags.transmit, definition.flags.update,
					definition.flags.readOnInit};
				const QString dpt = definition.datapointType.section(u' ', 0, 0);
				object.dpt = model::isValidDptRef(dpt) ? dpt : QString();
				object.catalogRef = definition.refId;
				KESTREL_TRY(const auto objectId, model::addComObject(project, deviceId, object));

				for (const std::uint16_t raw : addresses)
				{
					std::optional<model::Id<model::GroupAddress>> address;
					for (const model::GroupAddress& candidate : project.groupAddresses().rows())
					{
						if (candidate.raw == raw)
						{
							address = candidate.id;
						}
					}
					if (!address)
					{
						const knx::GroupAddress ga(raw);
						KESTREL_TRY(const auto created, model::addGroupAddress(project, ga,
							QString::fromStdString(ga.toString())));
						address = created;
					}
					KESTREL_TRY(const auto linkId, model::link(project, objectId, *address));
					(void)linkId; // the first link of an object is its sending link
				}
			}
			return ok();
		});
	if (!status)
	{
		return Unexpected{status.error()};
	}
	return warnings;
}

} // namespace kestrel::appconfig
