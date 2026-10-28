#pragma once

// Com objects of a catalog device (FR-DEV-2, FR-GA-3; specs/08-gui.md §3): one row per active catalog com object
// (ComObjectRef), keyed by its ref id. The linked group addresses come from the project: the model ComObject of
// the device whose catalogRef is the row's ref id (created on demand by the first link).
// Columns: # | Text | Function | Channel | Size | Flags | DPT | Group addresses (sending address first).
// Rows change with setObjects() (keyed diff); project edits only refresh cells. Inactive rows are linked com objects
// that the parameters deactivated: their links stay in the project but are not downloaded (shown greyed).

#include "kestrel/catalog/Model.hpp"
#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/viewmodels/KeyedTableModel.hpp"

#include <QHash>
#include <QSet>

#include <optional>
#include <vector>

namespace kestrel::viewmodels
{

class ProjectSession;

class CatalogComObjectModel : public KeyedTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		NumberColumn,
		TextColumn,
		FunctionColumn,
		ChannelColumn,
		SizeColumn,
		FlagsColumn,
		DptColumn,
		GroupAddressesColumn,
	};

	enum ExtraRole
	{
		LinkedRole = KeyRole + 1, ///< bool: at least one group address
		ComObjectRole,            ///< "co-…" of the model com object, or ""
		InactiveRole,             ///< bool: linked but not active with the parameters
	};

	CatalogComObjectModel(ProjectSession* session, model::Id<model::Device> device, QObject* parent = nullptr);

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

	/// Replaces the shown objects (incremental). `channelTexts`: channel id -> display text. `inactive`: ref ids
	/// of rows shown greyed.
	void setObjects(std::vector<catalog::EffectiveComObject> objects, QHash<QString, QString> channelTexts,
		QSet<QString> inactive = {});
	bool isInactive(const QString& refId) const { return m_inactive.contains(refId); }
	const std::vector<catalog::EffectiveComObject>& objects() const { return m_objects; }
	const catalog::EffectiveComObject* object(const QString& refId) const;
	QString channelText(const catalog::EffectiveComObject& object) const;
	/// The device's model com object for a catalog ref id.
	std::optional<model::Id<model::ComObject>> modelObject(const QString& refId) const;
	QString groupAddressesText(const QString& refId) const;

protected:
	QHash<int, QByteArray> extraRoleNames() const override;

private:
	void onChanged(const model::ChangeSet& changeSet);

	ProjectSession* m_session;
	model::Id<model::Device> m_device;
	std::vector<catalog::EffectiveComObject> m_objects;
	QHash<QString, std::size_t> m_byRef;
	QHash<QString, QString> m_channelTexts;
	QSet<QString> m_inactive;
};

} // namespace kestrel::viewmodels
