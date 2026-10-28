#include "kestrel/viewmodels/DeviceParameterModel.hpp"

#include "kestrel/viewmodels/ConfigurationHooks.hpp"

#include <QSet>
#include <QVariantMap>

#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

namespace
{

class RowBuilder
{
public:
	RowBuilder(const catalog::ApplicationProgram& program, const model::Device& device,
		const appconfig::Evaluation& evaluation, const QString& language)
		: m_program(program), m_device(device), m_evaluation(evaluation), m_language(language),
		  m_values(parameterValues(program, device))
	{
	}

	std::vector<ParameterRow> build()
	{
		std::vector<ParameterRow> rows;
		walk(m_program.dynamic, 0, rows);
		return rows;
	}

private:
	QString text(const QString& id, const QString& fallback) const
	{
		const QString translated = id.isEmpty() ? QString() : m_program.text(id, u"Text"_s, m_language);
		return translated.isEmpty() ? fallback : translated;
	}

	void walk(const catalog::DynamicNode& node, int depth, std::vector<ParameterRow>& rows)
	{
		switch (node.kind)
		{
			case catalog::DynamicKind::Channel:
			case catalog::DynamicKind::ParameterBlock:
			{
				const QString id = node.attribute(u"Id"_s);
				QString title = text(id, node.attribute(u"Text"_s));
				if (title.isEmpty())
					title = node.attribute(u"Name"_s);
				std::vector<ParameterRow> children;
				for (const catalog::DynamicNode& child : node.children)
					walk(child, depth + 1, children);
				const bool hasParameter = std::any_of(children.begin(), children.end(), [](const auto& r) { return !r.header; });
				if (!hasParameter)
					return;
				const QString key = (node.kind == catalog::DynamicKind::Channel ? u"channel:"_s : u"block:"_s) + id;
				if (!m_seen.contains(key) && !title.isEmpty())
				{
					m_seen.insert(key);
					ParameterRow header;
					header.key = key;
					header.header = true;
					header.depth = depth;
					header.text = title;
					rows.push_back(std::move(header));
				}
				for (ParameterRow& child : children)
					rows.push_back(std::move(child));
				return;
			}
			case catalog::DynamicKind::ParameterRefRef:
			{
				const QString refId = node.attribute(u"RefId"_s);
				if (refId.isEmpty() || m_seen.contains(refId) || !m_evaluation.isParameterRefActive(refId))
					return;
				if (auto row = parameter(refId, depth))
				{
					m_seen.insert(refId);
					rows.push_back(std::move(*row));
				}
				return;
			}
			default:
				for (const catalog::DynamicNode& child : node.children)
					walk(child, depth, rows);
				return;
		}
	}

	std::optional<ParameterRow> parameter(const QString& refId, int depth) const
	{
		const catalog::ParameterRef* ref = m_program.findParameterRef(refId);
		const catalog::Parameter* parameter = ref ? m_program.findParameter(ref->refId) : nullptr;
		if (!parameter)
			return std::nullopt;
		const QString access = ref->access.value_or(parameter->access);
		if (access == u"None"_s)
			return std::nullopt;
		ParameterRow row;
		row.key = refId;
		row.refId = refId;
		row.depth = depth;
		row.text = text(refId, QString());
		if (row.text.isEmpty())
			row.text = text(parameter->id, ref->text.value_or(parameter->text));
		if (row.text.isEmpty())
			row.text = parameter->name;
		row.readOnly = access == u"Read"_s;
		row.value = appconfig::valueOf(m_program, m_values, refId);
		row.defaultValue = appconfig::valueOf(m_program, {}, refId);
		row.isDefault = !m_device.params.contains(refId);
		const catalog::ParameterType* type = m_program.findParameterType(parameter->parameterType);
		switch (type ? type->kind : catalog::ParameterTypeKind::Unsupported)
		{
			case catalog::ParameterTypeKind::Restriction:
				row.editor = u"enum"_s;
				for (const catalog::EnumerationEntry& entry : type->enumeration)
				{
					row.options.append(
						QVariantMap{{u"text"_s, text(entry.id, entry.text)}, {u"value"_s, entry.value}});
				}
				break;
			case catalog::ParameterTypeKind::Number:
				row.editor = u"number"_s;
				row.minimum = type->minimum.value_or(0);
				row.maximum = type->maximum.value_or(
					type->sizeInBit > 0 && type->sizeInBit < 63 ? (qint64{1} << type->sizeInBit) - 1 : std::numeric_limits<qint32>::max());
				break;
			case catalog::ParameterTypeKind::Float:
				row.editor = u"float"_s;
				row.minimumFloat = type->minimumFloat.value_or(-std::numeric_limits<double>::max());
				row.maximumFloat = type->maximumFloat.value_or(std::numeric_limits<double>::max());
				break;
			case catalog::ParameterTypeKind::Text: row.editor = u"text"_s; break;
			case catalog::ParameterTypeKind::None: row.editor = u"none"_s; break;
			case catalog::ParameterTypeKind::Unsupported:
				row.editor = u"unsupported"_s;
				row.readOnly = true;
				break;
		}
		return row;
	}

	const catalog::ApplicationProgram& m_program;
	const model::Device& m_device;
	const appconfig::Evaluation& m_evaluation;
	QString m_language;
	appconfig::ParameterValues m_values;
	QSet<QString> m_seen;
};

} // namespace

std::vector<ParameterRow> parameterRows(const catalog::ApplicationProgram& program, const model::Device& device,
	const appconfig::Evaluation& evaluation, const QString& language)
{
	return RowBuilder(program, device, evaluation, language).build();
}

DeviceParameterModel::DeviceParameterModel(QObject* parent) : KeyedTableModel({tr("Parameter"), tr("Value")}, parent) {}

QHash<int, QByteArray> DeviceParameterModel::extraRoleNames() const
{
	return {
		{KindRole, "kind"},
		{DepthRole, "depth"},
		{TextRole, "text"},
		{EditorRole, "editor"},
		{OptionsRole, "options"},
		{MinimumRole, "minimum"},
		{MaximumRole, "maximum"},
		{ValueRole, "value"},
		{DefaultValueRole, "defaultValue"},
		{ReadOnlyRole, "readOnly"},
		{IsDefaultRole, "isDefault"},
		{ValueTextRole, "valueText"},
	};
}

void DeviceParameterModel::setRows(std::vector<ParameterRow> rows)
{
	QStringList keys;
	QHash<QString, std::size_t> byKey;
	std::vector<ParameterRow> unique;
	for (ParameterRow& r : rows)
	{
		if (byKey.contains(r.key))
			continue;
		byKey.insert(r.key, unique.size());
		keys.append(r.key);
		unique.push_back(std::move(r));
	}
	QStringList kept;
	for (const QString& key : this->keys())
	{
		if (byKey.contains(key))
			kept.append(key);
	}
	syncKeys(kept);
	m_rows = std::move(unique);
	m_byKey = std::move(byKey);
	syncKeys(keys);
	refreshAll();
}

const ParameterRow* DeviceParameterModel::row(const QString& key) const
{
	const auto it = m_byKey.constFind(key);
	return it == m_byKey.cend() ? nullptr : &m_rows[*it];
}

int DeviceParameterModel::parameterCount() const
{
	return static_cast<int>(std::count_if(m_rows.begin(), m_rows.end(), [](const ParameterRow& r) { return !r.header; }));
}

QVariant DeviceParameterModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};
	const ParameterRow* r = row(keyAt(index.row()));
	if (!r)
		return {};
	QString valueText = r->value;
	for (const QVariant& option : r->options)
	{
		const QVariantMap map = option.toMap();
		if (map.value(u"value"_s).toString() == r->value)
			valueText = map.value(u"text"_s).toString();
	}
	switch (role)
	{
		case Qt::DisplayRole: return index.column() == 0 ? r->text : valueText;
		case KeyRole: return r->key;
		case KindRole: return r->header ? u"block"_s : u"parameter"_s;
		case DepthRole: return r->depth;
		case TextRole: return r->text;
		case EditorRole: return r->editor;
		case OptionsRole: return r->options;
		case MinimumRole: return r->editor == u"float"_s ? QVariant(r->minimumFloat) : QVariant(r->minimum);
		case MaximumRole: return r->editor == u"float"_s ? QVariant(r->maximumFloat) : QVariant(r->maximum);
		case ValueRole: return r->value;
		case DefaultValueRole: return r->defaultValue;
		case ReadOnlyRole: return r->readOnly;
		case IsDefaultRole: return r->isDefault;
		case ValueTextRole: return valueText;
		default: return {};
	}
}

} // namespace kestrel::viewmodels
