#pragma once

// Properties dock: a form for the current selection (the project itself when nothing is selected).
// Edits name, description, number, DPT, the place of a device and the project's group address style.
// All properties share one NOTIFY signal (`subjectChanged`), raised when the selection changes or a
// ChangeSet touches the subject.

#include "kestrel/viewmodels/PanelVM.hpp"

#include <QStringList>

namespace kestrel::viewmodels
{

class PropertiesPanelVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QString ref READ ref NOTIFY subjectChanged)
	Q_PROPERTY(QString kind READ kind NOTIFY subjectChanged)
	Q_PROPERTY(QString kindLabel READ kindLabel NOTIFY subjectChanged)
	Q_PROPERTY(QString title READ title NOTIFY subjectChanged)
	Q_PROPERTY(QString name READ name NOTIFY subjectChanged)
	Q_PROPERTY(bool nameEditable READ nameEditable NOTIFY subjectChanged)
	Q_PROPERTY(QString description READ description NOTIFY subjectChanged)
	Q_PROPERTY(bool descriptionEditable READ descriptionEditable NOTIFY subjectChanged)
	Q_PROPERTY(QString descriptionLabel READ descriptionLabel NOTIFY subjectChanged)
	Q_PROPERTY(QString number READ number NOTIFY subjectChanged)
	Q_PROPERTY(bool numberVisible READ numberVisible NOTIFY subjectChanged)
	Q_PROPERTY(bool numberEditable READ numberEditable NOTIFY subjectChanged)
	Q_PROPERTY(QString address READ address NOTIFY subjectChanged)
	Q_PROPERTY(QString dpt READ dpt NOTIFY subjectChanged)
	Q_PROPERTY(bool dptVisible READ dptVisible NOTIFY subjectChanged)
	Q_PROPERTY(bool placeVisible READ placeVisible NOTIFY subjectChanged)
	Q_PROPERTY(QStringList placeOptions READ placeOptions NOTIFY subjectChanged)
	Q_PROPERTY(int placeIndex READ placeIndex NOTIFY subjectChanged)
	Q_PROPERTY(bool gaStyleVisible READ gaStyleVisible NOTIFY subjectChanged)
	Q_PROPERTY(int gaStyle READ gaStyle NOTIFY subjectChanged)
	Q_PROPERTY(QString details READ details NOTIFY subjectChanged)

public:
	explicit PropertiesPanelVM(const Services& services, QObject* parent = nullptr);

	model::EntityRef subject() const { return m_subject; }
	QString ref() const { return m_subject.toString(); }
	QString kind() const;
	QString kindLabel() const;
	QString title() const;
	QString name() const;
	bool nameEditable() const { return m_subject.kind != model::EntityKind::Link; }
	QString description() const;
	bool descriptionEditable() const;
	/// "Function" for group objects, else "Description".
	QString descriptionLabel() const;
	QString number() const;
	bool numberVisible() const;
	bool numberEditable() const { return m_subject.kind == model::EntityKind::Device; }
	QString address() const;
	QString dpt() const;
	bool dptVisible() const;
	bool placeVisible() const { return m_subject.kind == model::EntityKind::Device; }
	/// "(not placed)" followed by every building part, indented by depth.
	QStringList placeOptions() const;
	int placeIndex() const;
	bool gaStyleVisible() const { return m_subject.kind == model::EntityKind::Project; }
	/// 0 = 3-level, 1 = 2-level, 2 = free.
	int gaStyle() const;
	/// Read-only facts (size, flags, links …), one per line.
	QString details() const;

	Q_INVOKABLE bool setName(const QString& name);
	Q_INVOKABLE bool setDescription(const QString& description);
	Q_INVOKABLE bool setNumber(const QString& number);
	Q_INVOKABLE bool setDpt(const QString& dpt);
	Q_INVOKABLE bool setPlaceIndex(int index);
	Q_INVOKABLE bool setGaStyle(int style);

Q_SIGNALS:
	void subjectChanged();

private:
	void updateSubject();
	void onChanged(const model::ChangeSet& changeSet);
	std::vector<model::Id<model::BuildingPart>> placeIds() const;

	model::EntityRef m_subject = model::EntityRef::project();
};

} // namespace kestrel::viewmodels
