#include "kestrel/viewmodels/TopologyPanelVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/TopologyTreeModel.hpp"

#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

namespace
{

std::optional<unsigned> lowestFree(const std::set<unsigned>& used, unsigned first, unsigned last)
{
	for (unsigned n = first; n <= last; ++n)
	{
		if (!used.contains(n))
			return n;
	}
	return std::nullopt;
}

} // namespace

TopologyPanelVM::TopologyPanelVM(const Services& services, QObject* parent)
	: TreePanelVM(services, u"topology"_s, parent)
{
	setModel(new TopologyTreeModel(services.session));
}

TopologyTreeModel* TopologyPanelVM::topologyModel() const
{
	return static_cast<TopologyTreeModel*>(treeModel());
}

std::optional<model::Id<model::Area>> TopologyPanelVM::areaFor(model::EntityRef ref) const
{
	if (!project().contains(ref))
		return std::nullopt;
	switch (ref.kind)
	{
		case model::EntityKind::Area: return ref.as<model::Area>();
		case model::EntityKind::Line: return project().lines().at(ref.as<model::Line>()).area;
		case model::EntityKind::Device:
			return project().lines().at(project().devices().at(ref.as<model::Device>()).line).area;
		default: return std::nullopt;
	}
}

std::optional<model::Id<model::Line>> TopologyPanelVM::lineFor(model::EntityRef ref) const
{
	if (!project().contains(ref))
		return std::nullopt;
	switch (ref.kind)
	{
		case model::EntityKind::Line: return ref.as<model::Line>();
		case model::EntityKind::Device: return project().devices().at(ref.as<model::Device>()).line;
		default: return std::nullopt;
	}
}

QString TopologyPanelVM::addArea(const QString& name)
{
	std::set<unsigned> used;
	for (const model::Area& area : project().areas())
		used.insert(area.number);
	const auto number = lowestFree(used, 1, model::kMaxAreaNumber);
	if (!number)
	{
		setError(tr("All area numbers are in use."));
		return {};
	}
	const QString text = name.trimmed().isEmpty() ? tr("Area %1").arg(*number) : name.trimmed();
	const auto id = check(model::addArea(project(), *number, text));
	return id ? id->toString() : QString();
}

QString TopologyPanelVM::addLine(const QString& ref, const QString& name)
{
	const auto parsed = parseRef(ref);
	const auto area = parsed ? areaFor(*parsed) : std::nullopt;
	if (!area)
	{
		if (parsed)
			setError(tr("Select an area to add a line to."));
		return {};
	}
	std::set<unsigned> used;
	for (const auto line : project().linesOf(*area))
		used.insert(project().lines().at(line).number);
	const auto number = lowestFree(used, 1, model::kMaxLineNumber);
	if (!number)
	{
		setError(tr("All line numbers of this area are in use."));
		return {};
	}
	const QString text = name.trimmed().isEmpty() ? tr("Line %1").arg(*number) : name.trimmed();
	const auto id = check(model::addLine(project(), *area, *number, text));
	return id ? id->toString() : QString();
}

QString TopologyPanelVM::addDevice(const QString& ref, const QString& name)
{
	const auto parsed = parseRef(ref);
	const auto line = parsed ? lineFor(*parsed) : std::nullopt;
	if (!line)
	{
		if (parsed)
			setError(tr("Select a line to add a device to."));
		return {};
	}
	std::set<unsigned> used;
	for (const auto device : project().devicesOf(*line))
	{
		if (const auto number = project().devices().at(device).number)
			used.insert(*number);
	}
	const auto number = lowestFree(used, 1, model::kMaxDeviceNumber);
	if (!number)
	{
		setError(tr("All device numbers of this line are in use."));
		return {};
	}
	const QString text = name.trimmed().isEmpty() ? tr("Device %1").arg(*number) : name.trimmed();
	const auto id = check(model::addDevice(project(), *line, *number, text));
	return id ? id->toString() : QString();
}

bool TopologyPanelVM::setDeviceNumber(const QString& deviceRef, const QString& number)
{
	const auto parsed = parseRef(deviceRef);
	if (!parsed)
		return false;
	if (!parsed->is<model::Device>())
	{
		setError(tr("Not a device: %1").arg(deviceRef));
		return false;
	}
	std::optional<unsigned> value;
	if (!number.trimmed().isEmpty())
	{
		bool ok = false;
		const uint parsedNumber = number.trimmed().toUInt(&ok);
		if (!ok || parsedNumber > model::kMaxDeviceNumber)
		{
			setError(tr("Device numbers range from 0 to %1.").arg(model::kMaxDeviceNumber));
			return false;
		}
		value = parsedNumber;
	}
	return check(model::setDeviceNumber(project(), parsed->as<model::Device>(), value));
}

QStringList TopologyPanelVM::contextActions(model::EntityRef ref) const
{
	const QString sep = ActionRegistry::kSeparator;
	switch (ref.kind)
	{
		case model::EntityKind::Area: return {u"topology.addLine"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		case model::EntityKind::Line: return {u"topology.addDevice"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		case model::EntityKind::Device:
			return {u"device.open"_s, u"topology.addDevice"_s, sep, u"edit.rename"_s, u"edit.delete"_s};
		default: return {u"topology.addArea"_s};
	}
}

} // namespace kestrel::viewmodels
