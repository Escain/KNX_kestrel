#include "kestrel/viewmodels/DeviceEditorVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ComObjectTableModel.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <QVariantMap>

#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;

DeviceEditorVM::DeviceEditorVM(const Services& services, model::Id<model::Device> device, QObject* parent)
	: PanelVM(services, u"deviceEditor"_s, parent), m_device(device), m_model(new ComObjectTableModel(services.session, device, this))
{
	connect(session(), &ProjectSession::changed, this, &DeviceEditorVM::onChanged);
	connect(session(), &ProjectSession::projectReplaced, this,
		[this]
		{
			if (!exists())
				Q_EMIT deviceRemoved();
		});
}

bool DeviceEditorVM::exists() const
{
	return project().devices().contains(m_device);
}

QString DeviceEditorVM::title() const
{
	return exists() ? entityLabel(project(), m_device) : QString();
}

QString DeviceEditorVM::address() const
{
	return individualAddressText(project(), m_device);
}

QString DeviceEditorVM::name() const
{
	return exists() ? project().devices().at(m_device).name : QString();
}

QAbstractItemModel* DeviceEditorVM::comObjects() const
{
	return m_model;
}

int DeviceEditorVM::nextNumber() const
{
	std::set<unsigned> used;
	for (const auto id : project().comObjectsOf(m_device))
		used.insert(project().comObjects().at(id).number);
	unsigned number = 0;
	while (used.contains(number))
		++number;
	return static_cast<int>(number);
}

QString DeviceEditorVM::current() const
{
	return m_current ? model::EntityRef(*m_current).toString() : QString();
}

QString DeviceEditorVM::currentLabel() const
{
	if (!m_current || !project().comObjects().contains(*m_current))
		return {};
	const model::ComObject& object = project().comObjects().at(*m_current);
	return u"%1: %2"_s.arg(object.number).arg(object.name);
}

QVariantList DeviceEditorVM::currentLinks() const
{
	QVariantList result;
	if (!m_current || !project().comObjects().contains(*m_current))
		return result;
	auto links = project().linksOfComObject(*m_current);
	std::stable_partition(links.begin(), links.end(), [&](auto id) { return project().links().at(id).sending; });
	for (const auto id : links)
	{
		const model::Link& link = project().links().at(id);
		const model::GroupAddress& address = project().groupAddresses().at(link.groupAddress);
		result.append(QVariantMap{
			{u"ref"_s, model::EntityRef(id).toString()},
			{u"address"_s, groupAddressText(project(), address.raw)},
			{u"name"_s, address.name},
			{u"sending"_s, link.sending},
		});
	}
	return result;
}

QVariantList DeviceEditorVM::sizeOptions()
{
	QVariantList options;
	for (const unsigned bits : {1u, 2u, 4u, 8u, 16u, 24u, 32u, 48u, 64u, 112u})
		options.append(QVariantMap{{u"text"_s, sizeText(bits)}, {u"value"_s, bits}});
	return options;
}

std::optional<model::Id<model::ComObject>> DeviceEditorVM::comObjectFor(const QString& ref)
{
	const auto parsed = parseRef(ref);
	if (!parsed)
		return std::nullopt;
	if (!parsed->is<model::ComObject>() || project().comObjects().at(parsed->as<model::ComObject>()).device != m_device)
	{
		setError(tr("Not a group object of this device: %1").arg(ref));
		return std::nullopt;
	}
	return parsed->as<model::ComObject>();
}

std::optional<model::Id<model::Link>> DeviceEditorVM::linkFor(const QString& ref)
{
	const auto parsed = parseRef(ref);
	if (!parsed)
		return std::nullopt;
	if (!parsed->is<model::Link>())
	{
		setError(tr("Not an association: %1").arg(ref));
		return std::nullopt;
	}
	return parsed->as<model::Link>();
}

QString DeviceEditorVM::addComObject(const QString& name, const QString& function, int sizeInBit, const QString& flags, const QString& dpt)
{
	const auto parsedFlags = model::ComObjectFlags::parse(flags.trimmed().toUpper());
	if (!parsedFlags)
	{
		setError(tr("Flags are letters of CRWTUI: %1").arg(flags));
		return {};
	}
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return {};
	}
	if (sizeInBit <= 0)
	{
		setError(tr("Invalid size."));
		return {};
	}
	model::ComObject object;
	object.number = static_cast<unsigned>(nextNumber());
	object.name = name.trimmed().isEmpty() ? tr("Object %1").arg(object.number) : name.trimmed();
	object.function = function.trimmed();
	object.sizeInBit = static_cast<unsigned>(sizeInBit);
	object.flags = *parsedFlags;
	object.dpt = *normalized;
	const auto id = check(model::addComObject(project(), m_device, std::move(object)));
	if (!id)
		return {};
	setCurrent(model::EntityRef(*id).toString());
	return model::EntityRef(*id).toString();
}

bool DeviceEditorVM::removeComObject(const QString& ref)
{
	const auto id = comObjectFor(ref);
	return id && check(model::removeComObject(project(), *id));
}

void DeviceEditorVM::setCurrentIndex(const QModelIndex& index)
{
	setCurrent(m_model->refString(m_model->index(index.row(), 0)));
}

void DeviceEditorVM::setCurrent(const QString& ref)
{
	std::optional<model::Id<model::ComObject>> next;
	if (const auto parsed = model::EntityRef::parse(ref); parsed && parsed->is<model::ComObject>()
		&& project().comObjects().contains(parsed->as<model::ComObject>()))
	{
		next = parsed->as<model::ComObject>();
	}
	if (next != m_current)
	{
		m_current = next;
		Q_EMIT currentChanged();
	}
	if (m_current)
	{
		selection()->select(*m_current, origin());
	}
}

bool DeviceEditorVM::linkGroupAddress(const QString& comObjectRef, const QString& address)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto raw = parseGroupAddress(address);
	if (!raw)
	{
		setError(tr("Not a group address: %1").arg(address));
		return false;
	}
	const auto& addresses = project().groupAddresses();
	const auto found = std::find_if(addresses.begin(), addresses.end(), [&](const auto& ga) { return ga.raw == *raw; });
	if (found == addresses.end())
	{
		setError(tr("Group address %1 does not exist.").arg(groupAddressText(project(), *raw)));
		return false;
	}
	return static_cast<bool>(check(model::link(project(), *object, found->id)));
}

bool DeviceEditorVM::unlink(const QString& linkRef)
{
	const auto id = linkFor(linkRef);
	return id && check(model::unlink(project(), *id));
}

bool DeviceEditorVM::setSending(const QString& linkRef)
{
	const auto id = linkFor(linkRef);
	return id && check(model::setSendingLink(project(), *id));
}

bool DeviceEditorVM::setFlags(const QString& comObjectRef, const QString& flags)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto parsed = model::ComObjectFlags::parse(flags.trimmed().toUpper());
	if (!parsed)
	{
		setError(tr("Flags are letters of CRWTUI: %1").arg(flags));
		return false;
	}
	return check(project().edit(tr("Set flags"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::flags, *parsed);
			return ok();
		}));
}

bool DeviceEditorVM::setDpt(const QString& comObjectRef, const QString& dpt)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return false;
	}
	return check(project().edit(tr("Set datapoint type"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::dpt, *normalized);
			return ok();
		}));
}

bool DeviceEditorVM::setSize(const QString& comObjectRef, int sizeInBit)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	if (sizeInBit <= 0 || static_cast<unsigned>(sizeInBit) > model::kMaxComObjectSizeInBit)
	{
		setError(tr("Invalid size."));
		return false;
	}
	return check(project().edit(tr("Set size"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::sizeInBit, static_cast<unsigned>(sizeInBit));
			return ok();
		}));
}

void DeviceEditorVM::onChanged(const model::ChangeSet& changeSet)
{
	if (changeSet.contains(model::EntityRef(m_device)) && !exists())
	{
		Q_EMIT deviceRemoved();
		return;
	}
	if (m_current && !project().comObjects().contains(*m_current))
	{
		m_current.reset();
	}
	Q_EMIT deviceChanged();
	Q_EMIT currentChanged();
}

} // namespace kestrel::viewmodels
