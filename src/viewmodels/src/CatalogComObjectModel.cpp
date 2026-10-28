#include "kestrel/viewmodels/CatalogComObjectModel.hpp"

#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

CatalogComObjectModel::CatalogComObjectModel(ProjectSession* session, model::Id<model::Device> device, QObject* parent)
	: KeyedTableModel({tr("#"), tr("Text"), tr("Function"), tr("Channel"), tr("Size"), tr("Flags"), tr("DPT"),
						  tr("Group addresses")},
		  parent)
	, m_session(session)
	, m_device(device)
{
	connect(session, &ProjectSession::changed, this, &CatalogComObjectModel::onChanged);
	connect(session, &ProjectSession::projectReplaced, this, [this] { refreshAll(); });
}

QHash<int, QByteArray> CatalogComObjectModel::extraRoleNames() const
{
	return {{LinkedRole, "linked"}, {ComObjectRole, "comObject"}, {InactiveRole, "inactive"}};
}

void CatalogComObjectModel::setObjects(
	std::vector<catalog::EffectiveComObject> objects, QHash<QString, QString> channelTexts, QSet<QString> inactive)
{
	m_inactive = std::move(inactive);
	QStringList keys;
	QHash<QString, std::size_t> byRef;
	std::vector<catalog::EffectiveComObject> unique;
	unique.reserve(objects.size());
	for (catalog::EffectiveComObject& object : objects)
	{
		if (byRef.contains(object.refId))
			continue;
		byRef.insert(object.refId, unique.size());
		keys.append(object.refId);
		unique.push_back(std::move(object));
	}
	// Removed rows still read their old data while they go; then the new data serves the new rows.
	m_channelTexts = std::move(channelTexts);
	QStringList kept;
	for (const QString& key : this->keys())
	{
		if (byRef.contains(key))
			kept.append(key);
	}
	syncKeys(kept);
	m_objects = std::move(unique);
	m_byRef = std::move(byRef);
	syncKeys(keys);
	refreshAll();
}

const catalog::EffectiveComObject* CatalogComObjectModel::object(const QString& refId) const
{
	const auto it = m_byRef.constFind(refId);
	return it == m_byRef.cend() ? nullptr : &m_objects[*it];
}

QString CatalogComObjectModel::channelText(const catalog::EffectiveComObject& object) const
{
	return m_channelTexts.value(object.channelId);
}

std::optional<model::Id<model::ComObject>> CatalogComObjectModel::modelObject(const QString& refId) const
{
	const model::Project& project = m_session->project();
	if (refId.isEmpty() || !project.devices().contains(m_device))
		return std::nullopt;
	for (const auto id : project.comObjectsOf(m_device))
	{
		if (project.comObjects().at(id).catalogRef == refId)
			return id;
	}
	return std::nullopt;
}

QString CatalogComObjectModel::groupAddressesText(const QString& refId) const
{
	const auto id = modelObject(refId);
	if (!id)
		return {};
	const model::Project& p = m_session->project();
	auto links = p.linksOfComObject(*id);
	std::stable_partition(links.begin(), links.end(), [&](auto link) { return p.links().at(link).sending; });
	QStringList texts;
	for (const auto link : links)
		texts.append(groupAddressText(p, p.groupAddresses().at(p.links().at(link).groupAddress).raw));
	return texts.join(u", "_s);
}

QVariant CatalogComObjectModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};
	const QString key = keyAt(index.row());
	const catalog::EffectiveComObject* o = object(key);
	if (!o)
		return {};
	switch (role)
	{
		case KeyRole: return key;
		case LinkedRole: return !groupAddressesText(key).isEmpty();
		case ComObjectRole:
		{
			const auto id = modelObject(key);
			return id ? model::EntityRef(*id).toString() : QString();
		}
		case InactiveRole: return m_inactive.contains(key);
		case Qt::ToolTipRole:
			if (m_inactive.contains(key))
			{
				return tr("Not active with the current parameters: its group addresses are not downloaded.\n%1")
					.arg(o->refId);
			}
			return u"%1\n%2"_s.arg(o->refId, o->name);
		case Qt::DisplayRole:
			switch (index.column())
			{
				case NumberColumn: return QString::number(o->number);
				case TextColumn: return o->text.isEmpty() ? o->name : o->text;
				case FunctionColumn: return o->functionText;
				case ChannelColumn: return channelText(*o);
				case SizeColumn: return o->sizeInBit ? sizeText(o->sizeInBit) : o->objectSize;
				case FlagsColumn: return o->flags.toString();
				case DptColumn: return o->datapointType;
				case GroupAddressesColumn: return groupAddressesText(key);
				default: return {};
			}
		default: return {};
	}
}

void CatalogComObjectModel::onChanged(const model::ChangeSet& changeSet)
{
	if (changeSet.touches(model::EntityKind::Link) || changeSet.touches(model::EntityKind::ComObject)
		|| changeSet.touches(model::EntityKind::GroupAddress) || changeSet.touches(model::EntityKind::Project))
	{
		refreshAll();
	}
}

} // namespace kestrel::viewmodels
