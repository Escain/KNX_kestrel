#pragma once

// Parameters of a catalog device (FR-DEV-1; specs/08-gui.md §3): the active ParameterRefs (appconfig::evaluate)
// grouped under their channels and ParameterBlocks, in the order of the application's Dynamic section.
// A flat list: header rows (kind "block") followed by their parameter rows (kind "parameter"), with a depth for
// indentation. Rows are keyed ("block:<id>", or the ParameterRef id), so an edit that shows or hides parameters
// only inserts or removes those rows.

#include "kestrel/appconfig/Evaluator.hpp"
#include "kestrel/catalog/Model.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/viewmodels/KeyedTableModel.hpp"

#include <QHash>
#include <QVariantList>

#include <optional>
#include <vector>

namespace kestrel::viewmodels
{

struct ParameterRow
{
	QString key;
	bool header = false;
	int depth = 0;
	QString text;
	// Parameters only:
	QString refId;
	QString editor; ///< "enum", "number", "float", "text", "none" (a label), "unsupported" (read-only text)
	QVariantList options; ///< enum: [{text, value}]
	qint64 minimum = 0;
	qint64 maximum = 0;
	double minimumFloat = 0;
	double maximumFloat = 0;
	QString value;        ///< current canonical value
	QString defaultValue; ///< value without a project override
	bool readOnly = false;
	bool isDefault = true; ///< no value stored in the project

	friend bool operator==(const ParameterRow&, const ParameterRow&) = default;
};

/// The rows of a device, built from its program and its evaluation.
std::vector<ParameterRow> parameterRows(const catalog::ApplicationProgram& program, const model::Device& device,
	const appconfig::Evaluation& evaluation, const QString& language);

class DeviceParameterModel : public KeyedTableModel
{
	Q_OBJECT

public:
	enum ExtraRole
	{
		KindRole = KeyRole + 1, ///< "block" | "parameter"
		DepthRole,
		TextRole,
		EditorRole,
		OptionsRole,
		MinimumRole,
		MaximumRole,
		ValueRole,
		DefaultValueRole,
		ReadOnlyRole,
		IsDefaultRole,
		ValueTextRole, ///< enum: the option text; else the value
	};

	explicit DeviceParameterModel(QObject* parent = nullptr);

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	void setRows(std::vector<ParameterRow> rows);
	const ParameterRow* row(const QString& key) const;
	int parameterCount() const;

protected:
	QHash<int, QByteArray> extraRoleNames() const override;

private:
	std::vector<ParameterRow> m_rows;
	QHash<QString, std::size_t> m_byKey;
};

} // namespace kestrel::viewmodels
