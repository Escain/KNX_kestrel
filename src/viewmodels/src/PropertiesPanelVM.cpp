#include "kestrel/viewmodels/PropertiesPanelVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <functional>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;

PropertiesPanelVM::PropertiesPanelVM(const Services& services, QObject* parent)
	: PanelVM(services, u"properties"_s, parent)
{
	connect(selection(), &SelectionService::selectionChanged, this, &PropertiesPanelVM::updateSubject);
	connect(session(), &ProjectSession::changed, this, &PropertiesPanelVM::onChanged);
	connect(session(), &ProjectSession::projectReplaced, this, &PropertiesPanelVM::updateSubject);
	updateSubject();
}

void PropertiesPanelVM::updateSubject()
{
	const auto current = selection()->current();
	m_subject = current && project().contains(*current) ? *current : model::EntityRef::project();
	Q_EMIT subjectChanged();
}

void PropertiesPanelVM::onChanged(const model::ChangeSet& changeSet)
{
	if (!project().contains(m_subject))
	{
		updateSubject();
		return;
	}
	// Labels include names and addresses of related entities: refresh on any change. The form is small.
	if (!changeSet.empty())
	{
		Q_EMIT subjectChanged();
	}
}

QString PropertiesPanelVM::kind() const
{
	return QString::fromLatin1(model::toString(m_subject.kind));
}

QString PropertiesPanelVM::kindLabel() const
{
	if (m_subject.kind == EntityKind::BuildingPart)
	{
		return buildingPartKindName(project().buildingParts().at(m_subject.as<model::BuildingPart>()).kind);
	}
	return kindName(m_subject.kind);
}

QString PropertiesPanelVM::title() const
{
	if (m_subject.kind == EntityKind::Project)
	{
		return session()->projectName();
	}
	return entityLabel(project(), m_subject);
}

QString PropertiesPanelVM::name() const
{
	return entityName(project(), m_subject);
}

QString PropertiesPanelVM::description() const
{
	switch (m_subject.kind)
	{
		case EntityKind::BuildingPart: return project().buildingParts().at(m_subject.as<model::BuildingPart>()).description;
		case EntityKind::Device: return project().devices().at(m_subject.as<model::Device>()).description;
		case EntityKind::GroupAddress: return project().groupAddresses().at(m_subject.as<model::GroupAddress>()).description;
		case EntityKind::ComObject: return project().comObjects().at(m_subject.as<model::ComObject>()).function;
		default: return {};
	}
}

bool PropertiesPanelVM::descriptionEditable() const
{
	return m_subject.kind == EntityKind::BuildingPart || m_subject.kind == EntityKind::Device
		|| m_subject.kind == EntityKind::GroupAddress || m_subject.kind == EntityKind::ComObject;
}

QString PropertiesPanelVM::descriptionLabel() const
{
	return m_subject.kind == EntityKind::ComObject ? tr("Function") : tr("Description");
}

QString PropertiesPanelVM::number() const
{
	switch (m_subject.kind)
	{
		case EntityKind::Area: return QString::number(project().areas().at(m_subject.as<model::Area>()).number);
		case EntityKind::Line: return QString::number(project().lines().at(m_subject.as<model::Line>()).number);
		case EntityKind::Device:
		{
			const auto number = project().devices().at(m_subject.as<model::Device>()).number;
			return number ? QString::number(*number) : QString();
		}
		case EntityKind::ComObject: return QString::number(project().comObjects().at(m_subject.as<model::ComObject>()).number);
		default: return {};
	}
}

bool PropertiesPanelVM::numberVisible() const
{
	return m_subject.kind == EntityKind::Area || m_subject.kind == EntityKind::Line
		|| m_subject.kind == EntityKind::Device || m_subject.kind == EntityKind::ComObject;
}

QString PropertiesPanelVM::address() const
{
	switch (m_subject.kind)
	{
		case EntityKind::Area: return QString::number(project().areas().at(m_subject.as<model::Area>()).number);
		case EntityKind::Line: return lineAddressText(project(), m_subject.as<model::Line>());
		case EntityKind::Device: return individualAddressText(project(), m_subject.as<model::Device>());
		case EntityKind::GroupRange:
			return groupRangeText(project(), project().groupRanges().at(m_subject.as<model::GroupRange>()));
		case EntityKind::GroupAddress:
			return groupAddressText(project(), project().groupAddresses().at(m_subject.as<model::GroupAddress>()).raw);
		default: return {};
	}
}

QString PropertiesPanelVM::dpt() const
{
	switch (m_subject.kind)
	{
		case EntityKind::GroupAddress: return project().groupAddresses().at(m_subject.as<model::GroupAddress>()).dpt;
		case EntityKind::ComObject: return project().comObjects().at(m_subject.as<model::ComObject>()).dpt;
		default: return {};
	}
}

bool PropertiesPanelVM::dptVisible() const
{
	return m_subject.kind == EntityKind::GroupAddress || m_subject.kind == EntityKind::ComObject;
}

std::vector<model::Id<model::BuildingPart>> PropertiesPanelVM::placeIds() const
{
	std::vector<model::Id<model::BuildingPart>> ids;
	std::function<void(std::optional<model::Id<model::BuildingPart>>)> visit =
		[&](std::optional<model::Id<model::BuildingPart>> parent)
	{
		for (const auto id : project().buildingPartsIn(parent))
		{
			ids.push_back(id);
			visit(id);
		}
	};
	visit(std::nullopt);
	return ids;
}

QStringList PropertiesPanelVM::placeOptions() const
{
	if (!placeVisible())
	{
		return {};
	}
	QStringList options{tr("(not placed)")};
	for (const auto id : placeIds())
	{
		int depth = 0;
		for (auto parent = project().buildingParts().at(id).parent; parent; parent = project().buildingParts().at(*parent).parent)
			++depth;
		options.append(QString(depth * 4, u' ') + project().buildingParts().at(id).name);
	}
	return options;
}

int PropertiesPanelVM::placeIndex() const
{
	if (!placeVisible())
	{
		return -1;
	}
	const auto place = project().devices().at(m_subject.as<model::Device>()).place;
	if (!place)
	{
		return 0;
	}
	const auto ids = placeIds();
	for (std::size_t i = 0; i < ids.size(); ++i)
	{
		if (ids[i] == *place)
			return static_cast<int>(i) + 1;
	}
	return 0;
}

int PropertiesPanelVM::gaStyle() const
{
	return static_cast<int>(project().settings().gaStyle);
}

QString PropertiesPanelVM::details() const
{
	QStringList lines;
	const model::Project& p = project();
	switch (m_subject.kind)
	{
		case EntityKind::Project:
			lines << countText(static_cast<qsizetype>(p.devices().size()), "%1 device", "%1 devices")
				  << countText(static_cast<qsizetype>(p.groupAddresses().size()), "%1 group address", "%1 group addresses");
			break;
		case EntityKind::Device:
		{
			const auto id = m_subject.as<model::Device>();
			lines << countText(static_cast<qsizetype>(p.comObjectsOf(id).size()), "%1 group object", "%1 group objects");
			const model::DeviceState& state = p.deviceState(id);
			lines << (state.iaProgrammed ? tr("Address programmed") : tr("Address not programmed"));
			if (!state.serial.isEmpty())
				lines << tr("Serial number: %1").arg(state.serial);
			break;
		}
		case EntityKind::ComObject:
		{
			const model::ComObject& object = p.comObjects().at(m_subject.as<model::ComObject>());
			lines << tr("Size: %1").arg(sizeText(object.sizeInBit)) << tr("Flags: %1").arg(object.flags.toString());
			break;
		}
		case EntityKind::GroupAddress:
		{
			const auto links = p.linksOfGroupAddress(m_subject.as<model::GroupAddress>());
			lines << countText(static_cast<qsizetype>(links.size()), "%1 association", "%1 associations");
			for (const auto link : links)
				lines << u"  "_s + entityLabel(p, p.links().at(link).comObject);
			break;
		}
		case EntityKind::GroupRange:
		{
			const model::GroupRange& range = p.groupRanges().at(m_subject.as<model::GroupRange>());
			lines << tr("Range: %1 – %2").arg(groupAddressText(p, range.start), groupAddressText(p, range.end));
			break;
		}
		default: break;
	}
	return lines.join(u'\n');
}

bool PropertiesPanelVM::setName(const QString& name)
{
	if (name.trimmed() == this->name())
	{
		return true;
	}
	return check(model::rename(project(), m_subject, name.trimmed()));
}

bool PropertiesPanelVM::setDescription(const QString& description)
{
	if (description == this->description())
	{
		return true;
	}
	const model::EntityRef subject = m_subject;
	const model::MergeKey key{u"description:"_s + subject.toString()};
	return check(project().edit(
		tr("Change description"),
		[&](model::Tx& tx) -> Status
		{
			switch (subject.kind)
			{
				case EntityKind::BuildingPart: tx.set(subject.as<model::BuildingPart>(), &model::BuildingPart::description, description); break;
				case EntityKind::Device: tx.set(subject.as<model::Device>(), &model::Device::description, description); break;
				case EntityKind::GroupAddress: tx.set(subject.as<model::GroupAddress>(), &model::GroupAddress::description, description); break;
				case EntityKind::ComObject: tx.set(subject.as<model::ComObject>(), &model::ComObject::function, description); break;
				default: return makeError(ErrorCode::InvalidArgument, "this object has no description");
			}
			return ok();
		},
		key));
}

bool PropertiesPanelVM::setNumber(const QString& number)
{
	if (number.trimmed() == this->number())
	{
		return true;
	}
	if (m_subject.kind != EntityKind::Device)
	{
		setError(tr("Only device numbers can be changed here."));
		return false;
	}
	std::optional<unsigned> value;
	if (!number.trimmed().isEmpty())
	{
		bool ok = false;
		const uint parsed = number.trimmed().toUInt(&ok);
		if (!ok || parsed > model::kMaxDeviceNumber)
		{
			setError(tr("Device numbers range from 0 to %1.").arg(model::kMaxDeviceNumber));
			return false;
		}
		value = parsed;
	}
	return check(model::setDeviceNumber(project(), m_subject.as<model::Device>(), value));
}

bool PropertiesPanelVM::setDpt(const QString& dpt)
{
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return false;
	}
	if (*normalized == this->dpt())
	{
		return true;
	}
	if (m_subject.kind == EntityKind::GroupAddress)
	{
		return check(model::setGroupAddressDpt(project(), m_subject.as<model::GroupAddress>(), *normalized));
	}
	if (m_subject.kind == EntityKind::ComObject)
	{
		const auto id = m_subject.as<model::ComObject>();
		return check(project().edit(tr("Set datapoint type"),
			[&](model::Tx& tx)
			{
				tx.set(id, &model::ComObject::dpt, *normalized);
				return ok();
			}));
	}
	setError(tr("This object has no datapoint type."));
	return false;
}

bool PropertiesPanelVM::setPlaceIndex(int index)
{
	if (!placeVisible() || index == placeIndex())
	{
		return placeVisible();
	}
	const auto ids = placeIds();
	if (index < 0 || static_cast<std::size_t>(index) > ids.size())
	{
		setError(tr("Unknown place."));
		return false;
	}
	std::optional<model::Id<model::BuildingPart>> place;
	if (index > 0)
	{
		place = ids[static_cast<std::size_t>(index - 1)];
	}
	return check(model::placeDevice(project(), m_subject.as<model::Device>(), place));
}

bool PropertiesPanelVM::setGaStyle(int style)
{
	if (style < 0 || style > 2)
	{
		setError(tr("Unknown group address style."));
		return false;
	}
	model::ProjectSettings settings = project().settings();
	const auto value = static_cast<knx::GroupAddressStyle>(style);
	if (settings.gaStyle == value)
	{
		return true;
	}
	settings.gaStyle = value;
	return check(model::updateSettings(project(), settings));
}

} // namespace kestrel::viewmodels
