#pragma once

// Device editor (central tab): the device's group objects and their group address links (FR-GA-3).
// M1 has no catalog, so group objects are defined manually.

#include "kestrel/viewmodels/PanelVM.hpp"

#include <QVariantList>

namespace kestrel::viewmodels
{

class ComObjectTableModel;

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

public:
	DeviceEditorVM(const Services& services, model::Id<model::Device> device, QObject* parent = nullptr);

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

Q_SIGNALS:
	void deviceChanged();
	void currentChanged();
	/// The device was deleted: the editor should close.
	void deviceRemoved();

private:
	void onChanged(const model::ChangeSet& changeSet);
	std::optional<model::Id<model::ComObject>> comObjectFor(const QString& ref);
	std::optional<model::Id<model::Link>> linkFor(const QString& ref);

	model::Id<model::Device> m_device;
	ComObjectTableModel* m_model;
	std::optional<model::Id<model::ComObject>> m_current;
};

} // namespace kestrel::viewmodels
