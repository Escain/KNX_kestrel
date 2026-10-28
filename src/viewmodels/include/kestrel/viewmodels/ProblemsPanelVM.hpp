#pragma once

// Problems dock: validation results with click-to-navigate.

#include "kestrel/viewmodels/PanelVM.hpp"

namespace kestrel::viewmodels
{

class ProblemsModel;

class ProblemsPanelVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
	Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
	Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
	Q_PROPERTY(int infoCount READ infoCount NOTIFY countsChanged)
	Q_PROPERTY(QString summary READ summary NOTIFY countsChanged)

public:
	explicit ProblemsPanelVM(const Services& services, QObject* parent = nullptr);

	QAbstractItemModel* model() const;
	ProblemsModel* problemsModel() const { return m_model; }
	int errorCount() const;
	int warningCount() const;
	int infoCount() const;
	/// "2 errors, 1 warning, 3 messages" / "No problems".
	QString summary() const;

	/// Selects the first subject of the diagnostic in `row` (the trees reveal it).
	Q_INVOKABLE void navigate(int row);
	/// Selects and activates (opens the device editor when the subject is a device or group object).
	Q_INVOKABLE void activate(int row);

Q_SIGNALS:
	void countsChanged();

private:
	ProblemsModel* m_model;
};

} // namespace kestrel::viewmodels
