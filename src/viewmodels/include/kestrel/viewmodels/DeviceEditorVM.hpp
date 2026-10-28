#pragma once

// Device editor (central tab): the device's group objects and their group address links (FR-GA-3).
//
// Two kinds of devices (specs/08-gui.md §3):
// - Catalog devices (appProgramRef set): the product, its application and the active com objects of the
//   application program come from the CatalogService (parsed off the GUI thread; catalogState "loading" until
//   then). The active set is chosen by the ActiveComObjectFilter hook (appconfig::evaluate by default).
//   Linking a group address creates the model ComObject on demand (catalogRef = ComObjectRef id) and, if the
//   typed address does not exist yet, the GroupAddress too, in one undo step. Unlinking the last group address
//   of a catalog com object removes its model ComObject in the same step.
// - Manual devices (M1): group objects are defined by hand.

#include "kestrel/viewmodels/ConfigurationHooks.hpp"
#include "kestrel/viewmodels/PanelVM.hpp"

#include <QHash>
#include <QVariantList>

#include <memory>

namespace kestrel::viewmodels
{

class CatalogComObjectModel;
class CatalogService;
class ComObjectTableModel;
class DeviceParameterModel;
class DeviceStatusService;

class DeviceEditorVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QString device READ deviceRef CONSTANT)
	Q_PROPERTY(bool exists READ exists NOTIFY deviceChanged)
	Q_PROPERTY(QString title READ title NOTIFY deviceChanged)
	Q_PROPERTY(QString address READ address NOTIFY deviceChanged)
	Q_PROPERTY(QString name READ name NOTIFY deviceChanged)
	Q_PROPERTY(QAbstractItemModel* comObjects READ comObjects CONSTANT)
	Q_PROPERTY(int nextNumber READ nextNumber NOTIFY deviceChanged)
	Q_PROPERTY(QString current READ current NOTIFY currentChanged)
	Q_PROPERTY(QString currentLabel READ currentLabel NOTIFY currentChanged)
	Q_PROPERTY(QVariantList currentLinks READ currentLinks NOTIFY currentChanged)
	Q_PROPERTY(QVariantList sizeOptions READ sizeOptions CONSTANT)
	Q_PROPERTY(bool catalogDevice READ isCatalogDevice NOTIFY deviceChanged)
	Q_PROPERTY(QString catalogState READ catalogState NOTIFY catalogChanged)
	Q_PROPERTY(QString catalogMessage READ catalogMessage NOTIFY catalogChanged)
	Q_PROPERTY(QString productText READ productText NOTIFY catalogChanged)
	Q_PROPERTY(QString orderNumber READ orderNumber NOTIFY catalogChanged)
	Q_PROPERTY(QString applicationText READ applicationText NOTIFY catalogChanged)
	Q_PROPERTY(QAbstractItemModel* catalogComObjects READ catalogComObjects CONSTANT)
	Q_PROPERTY(QString currentObject READ currentObject NOTIFY currentChanged)
	Q_PROPERTY(QVariantList groupAddressOptions READ groupAddressOptions NOTIFY groupAddressesChanged)
	Q_PROPERTY(QAbstractItemModel* parameters READ parameters CONSTANT)
	Q_PROPERTY(int parameterCount READ parameterCount NOTIFY parametersChanged)
	Q_PROPERTY(QString parameterWarnings READ parameterWarnings NOTIFY parametersChanged)
	Q_PROPERTY(int inactiveLinkedCount READ inactiveLinkedCount NOTIFY parametersChanged)
	Q_PROPERTY(QString downloadState READ downloadState NOTIFY downloadStateChanged)
	Q_PROPERTY(bool needsDownload READ needsDownload NOTIFY downloadStateChanged)

public:
	/// MIME format of a dragged entity ref ("ga-…"): the Group Address dock is a drag source.
	static inline const QString kRefMimeType = QStringLiteral("application/x-kestrel-ref");

	/// `catalog` may be null (catalog devices then report catalogState "missing"); `filter` empty =
	/// activeComObjects.
	DeviceEditorVM(const Services& services, model::Id<model::Device> device, CatalogService* catalog = nullptr,
		ActiveComObjectFilter filter = {}, QObject* parent = nullptr);
	~DeviceEditorVM() override;

	model::Id<model::Device> device() const { return m_device; }
	QString deviceRef() const { return model::EntityRef(m_device).toString(); }
	bool exists() const;
	QString title() const;
	QString address() const;
	QString name() const;
	QAbstractItemModel* comObjects() const;
	ComObjectTableModel* comObjectModel() const { return m_model; }
	/// Lowest unused group object number.
	int nextNumber() const;
	QString current() const;
	QString currentLabel() const;
	/// [{ref, address, name, sending}] of the current group object, sending first.
	QVariantList currentLinks() const;
	/// [{text, value}] for the size selector.
	static QVariantList sizeOptions();

	// --- Catalog devices -------------------------------------------------------------------------------------
	bool isCatalogDevice() const;
	/// "none" (manual device), "loading", "ready", "missing" (program not in the catalog) or "error".
	QString catalogState() const { return m_catalogState; }
	QString catalogMessage() const { return m_catalogMessage; }
	QString productText() const;
	QString orderNumber() const;
	/// "Kestrel Test Button 2-fold 1.0 (MV-07B0)"
	QString applicationText() const;
	QAbstractItemModel* catalogComObjects() const;
	CatalogComObjectModel* catalogModel() const { return m_catalogModel; }
	/// Current catalog com object (ComObjectRef id), "" if none.
	QString currentObject() const { return m_currentObject; }
	/// [{text: "1/2/3  Kitchen light", address: "1/2/3", ref}] of every group address, by address.
	QVariantList groupAddressOptions() const;
	/// Re-evaluates the parameters and re-applies the active com object filter. Called on device edits.
	void refreshComObjects();
	QAbstractItemModel* parameters() const;
	DeviceParameterModel* parameterModel() const { return m_parameters; }
	int parameterCount() const;
	/// Evaluation warnings (unsupported dynamic elements), one per line.
	QString parameterWarnings() const { return m_parameterWarnings; }
	/// Linked com objects that the parameters deactivated (shown greyed; their links are not downloaded).
	int inactiveLinkedCount() const { return m_inactiveLinkedCount; }

	/// Source of the "needs download" state (optional).
	void setDeviceStatus(DeviceStatusService* status);
	/// DeviceStatusService::describe(); empty without a status service or for manual devices.
	QString downloadState() const;
	bool needsDownload() const;

	/// Adds a group object with nextNumber(). `flags`: letters of "CRWTUI"; `dpt` may be empty.
	Q_INVOKABLE QString addComObject(const QString& name, const QString& function, int sizeInBit,
		const QString& flags = QStringLiteral("CWT"), const QString& dpt = {});
	Q_INVOKABLE bool removeComObject(const QString& ref);
	Q_INVOKABLE void setCurrentIndex(const QModelIndex& index);
	Q_INVOKABLE void setCurrent(const QString& ref);
	/// Links the group object to an existing group address given as text ("1/2/3").
	Q_INVOKABLE bool linkGroupAddress(const QString& comObjectRef, const QString& address);
	Q_INVOKABLE bool unlink(const QString& linkRef);
	Q_INVOKABLE bool setSending(const QString& linkRef);
	Q_INVOKABLE bool setFlags(const QString& comObjectRef, const QString& flags);
	Q_INVOKABLE bool setDpt(const QString& comObjectRef, const QString& dpt);
	Q_INVOKABLE bool setSize(const QString& comObjectRef, int sizeInBit);

	Q_INVOKABLE void setCurrentObjectRow(int row);
	Q_INVOKABLE void setCurrentObject(const QString& refId);
	/// Links a catalog com object to a group address given as "1/2/3" (created if missing, with the object's
	/// DPT), as "1/2/3 Name" (an entry of groupAddressOptions) or as a ref "ga-…". One undo step.
	Q_INVOKABLE bool linkObject(const QString& refId, const QString& address);
	/// Drop of a dragged ref on a row of the catalog table (a group address ref links it).
	Q_INVOKABLE bool dropOnObjectRow(int row, const QString& ref);
	/// Drop of a dragged ref on a row of the manual table.
	Q_INVOKABLE bool dropOnComObjectRow(int row, const QString& ref);
	/// Sets a parameter (ParameterRef id) to a canonical value, validated against its type; the default value
	/// resets it. One undo step; consecutive edits of one parameter merge.
	Q_INVOKABLE bool setParameter(const QString& refId, const QString& value);
	Q_INVOKABLE bool resetParameter(const QString& refId);

Q_SIGNALS:
	void deviceChanged();
	void currentChanged();
	void catalogChanged();
	void groupAddressesChanged();
	void parametersChanged();
	void downloadStateChanged();
	/// The device was deleted: the editor should close.
	void deviceRemoved();

private:
	void onChanged(const model::ChangeSet& changeSet);
	std::optional<model::Id<model::ComObject>> comObjectFor(const QString& ref);
	std::optional<model::Id<model::Link>> linkFor(const QString& ref);
	/// The model com object whose links are shown (manual current, or the current catalog object's).
	std::optional<model::Id<model::ComObject>> shownComObject() const;
	void loadCatalog();
	void setCatalogState(const QString& state, const QString& message);
	/// Group address for a link target text; creates nothing. nullopt + `raw` set: a new address is needed.
	std::optional<model::Id<model::GroupAddress>> findGroupAddress(const QString& text, std::optional<std::uint16_t>& raw);

	model::Id<model::Device> m_device;
	ComObjectTableModel* m_model;
	std::optional<model::Id<model::ComObject>> m_current;

	CatalogService* m_catalog;
	ActiveComObjectFilter m_filter;
	CatalogComObjectModel* m_catalogModel;
	QString m_catalogState = QStringLiteral("none");
	QString m_catalogMessage;
	QString m_catalogFile;
	QString m_loadedProgram;
	std::shared_ptr<const void> m_database; ///< keeps the program alive (catalog::LoadedDatabase)
	const catalog::ApplicationProgram* m_program = nullptr;
	QHash<QString, QString> m_channelTexts;
	QString m_currentObject;
	DeviceParameterModel* m_parameters;
	QString m_parameterWarnings;
	int m_inactiveLinkedCount = 0;
	DeviceStatusService* m_status = nullptr;
};

} // namespace kestrel::viewmodels
