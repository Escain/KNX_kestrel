#include "kestrel/viewmodels/CatalogPanelVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <QDir>
#include <QMimeData>
#include <QUrl>

#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

std::optional<unsigned> nextFreeDeviceNumber(const model::Project& project, model::Id<model::Line> line)
{
	std::set<unsigned> used;
	for (const auto device : project.devicesOf(line))
	{
		if (const auto number = project.devices().at(device).number)
			used.insert(*number);
	}
	for (unsigned n = 1; n <= model::kMaxDeviceNumber; ++n)
	{
		if (!used.contains(n))
			return n;
	}
	return std::nullopt;
}

// --- CatalogProductModel -----------------------------------------------------------------------------------

CatalogProductModel::CatalogProductModel(CatalogService* catalog, QObject* parent)
	: KeyedTableModel({tr("Product"), tr("Order number"), tr("Application"), tr("Version"), tr("Manufacturer")}, parent)
	, m_catalog(catalog)
{
	connect(m_catalog, &CatalogService::entriesChanged, this, &CatalogProductModel::sync);
	sync();
}

void CatalogProductModel::sync()
{
	QStringList keys;
	keys.reserve(static_cast<qsizetype>(m_catalog->entries().size()));
	for (const CatalogEntry& entry : m_catalog->entries())
		keys.append(entry.key);
	syncKeys(keys);
	refreshAll();
}

QVariant CatalogProductModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};
	const QString key = keyAt(index.row());
	const CatalogEntry* entry = m_catalog->entry(key);
	if (!entry)
		return {};
	switch (role)
	{
		case KeyRole: return key;
		case Qt::DisplayRole:
			switch (index.column())
			{
				case ProductColumn: return entry->productText;
				case OrderNumberColumn: return entry->orderNumber;
				case ApplicationColumn:
					if (entry->programId.isEmpty())
						return tr("(no application)");
					return entry->programAvailable ? entry->programName : tr("%1 (not in this file)").arg(entry->programName);
				case VersionColumn: return entry->versionText();
				case ManufacturerColumn: return entry->manufacturerId;
				default: return {};
			}
		case Qt::ToolTipRole:
			return tr("%1\nApplication %2 (%3)\nFile %4").arg(entry->productText, entry->programId, entry->maskVersion, entry->file);
		default: return {};
	}
}

Qt::ItemFlags CatalogProductModel::flags(const QModelIndex& index) const
{
	return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
}

QStringList CatalogProductModel::mimeTypes() const
{
	return {CatalogPanelVM::kMimeType};
}

QMimeData* CatalogProductModel::mimeData(const QModelIndexList& indexes) const
{
	if (indexes.isEmpty())
		return nullptr;
	auto* mime = new QMimeData;
	mime->setData(CatalogPanelVM::kMimeType, keyAt(indexes.constFirst().row()).toUtf8());
	return mime;
}

// --- CatalogFilterModel ------------------------------------------------------------------------------------

CatalogFilterModel::CatalogFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
	setFilterCaseSensitivity(Qt::CaseInsensitive);
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
	setDynamicSortFilter(true);
}

void CatalogFilterModel::setText(const QString& text)
{
	const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
	if (words == m_words)
		return;
	m_words = words;
	invalidateFilter();
}

bool CatalogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	// Every word must match some column ("ktb 2-fold").
	const int columns = sourceModel()->columnCount(sourceParent);
	for (const QString& word : m_words)
	{
		bool found = false;
		for (int column = 0; column < columns && !found; ++column)
		{
			const QString cell = sourceModel()->index(sourceRow, column, sourceParent).data().toString();
			found = cell.contains(word, Qt::CaseInsensitive);
		}
		if (!found)
			return false;
	}
	return true;
}

// --- CatalogPanelVM ----------------------------------------------------------------------------------------

CatalogPanelVM::CatalogPanelVM(const Services& services, CatalogService* catalog, QObject* parent)
	: PanelVM(services, u"catalog"_s, parent)
	, m_catalog(catalog)
	, m_products(new CatalogProductModel(catalog, this))
	, m_filtered(new CatalogFilterModel(this))
{
	m_filtered->setSourceModel(m_products);
	m_filtered->sort(CatalogProductModel::ProductColumn);
	connect(m_catalog, &CatalogService::entriesChanged, this, &CatalogPanelVM::onEntriesChanged);
	connect(m_catalog, &CatalogService::busyChanged, this, &CatalogPanelVM::statusChanged);
	connect(m_catalog, &CatalogService::fileIndexed, this, &CatalogPanelVM::statusChanged);
	connect(m_catalog, &CatalogService::importFailed, this,
		[this](const QString& file, const QString& message)
		{
			setError(tr("%1 is not a readable product file: %2").arg(file, message));
			Q_EMIT statusChanged();
		});
	connect(selection(), &SelectionService::selectionChanged, this, &CatalogPanelVM::onSelectionChanged);
	connect(session(), &ProjectSession::projectReplaced, this,
		[this]
		{
			m_target.reset();
			onSelectionChanged();
			Q_EMIT targetChanged();
		});
	connect(session(), &ProjectSession::changed, this,
		[this](const model::ChangeSet& changeSet)
		{
			if (changeSet.touches(model::EntityKind::Line) || changeSet.touches(model::EntityKind::Area))
			{
				if (m_target && !project().lines().contains(*m_target))
					m_target.reset();
				Q_EMIT targetChanged();
			}
		});
	onSelectionChanged();
}

QAbstractItemModel* CatalogPanelVM::model() const
{
	return m_filtered;
}

void CatalogPanelVM::setFilter(const QString& filter)
{
	if (filter == m_filter)
		return;
	m_filter = filter;
	m_filtered->setText(filter);
	Q_EMIT filterChanged();
}

bool CatalogPanelVM::isBusy() const
{
	return m_catalog->isBusy();
}

int CatalogPanelVM::productCount() const
{
	return static_cast<int>(m_catalog->entries().size());
}

QString CatalogPanelVM::status() const
{
	if (!m_catalog->isScanned())
		return {};
	const QStringList pending = m_catalog->pendingFiles();
	if (!pending.isEmpty())
		return tr("Reading %1…").arg(pending.join(u", "_s));
	std::set<QString> files;
	for (const CatalogEntry& entry : m_catalog->entries())
		files.insert(entry.file);
	if (files.empty())
		return tr("The catalog is empty. Import a product file (.knxprod) from the manufacturer.");
	return tr("%1 in %2").arg(countText(productCount(), "%1 product", "%1 products"),
		countText(static_cast<qsizetype>(files.size()), "%1 file", "%1 files"));
}

QString CatalogPanelVM::problems() const
{
	return m_catalog->problems().join(u'\n');
}

QString CatalogPanelVM::directory() const
{
	return QDir::toNativeSeparators(m_catalog->directory());
}

QString CatalogPanelVM::currentDetails() const
{
	const CatalogEntry* entry = m_catalog->entry(m_currentKey);
	if (!entry)
		return {};
	QStringList lines{entry->productText};
	if (!entry->orderNumber.isEmpty())
		lines.append(tr("Order number %1").arg(entry->orderNumber));
	if (!entry->programId.isEmpty())
		lines.append(tr("Application %1 %2, %3").arg(entry->programName, entry->versionText(), entry->maskVersion));
	lines.append(tr("File %1").arg(entry->file));
	return lines.join(u'\n');
}

std::optional<model::Id<model::Line>> CatalogPanelVM::targetLine() const
{
	if (m_target && project().lines().contains(*m_target))
		return m_target;
	return std::nullopt;
}

QString CatalogPanelVM::targetText() const
{
	const auto line = targetLine();
	if (!line)
		return {};
	return u"%1 %2"_s.arg(lineAddressText(project(), *line), project().lines().at(*line).name);
}

bool CatalogPanelVM::canAdd() const
{
	return m_catalog->entry(m_currentKey) != nullptr && targetLine().has_value();
}

void CatalogPanelVM::onSelectionChanged()
{
	const auto ref = selection()->current();
	if (!ref || !project().contains(*ref))
		return;
	std::optional<model::Id<model::Line>> line;
	if (ref->is<model::Line>())
		line = ref->as<model::Line>();
	else if (ref->is<model::Device>())
		line = project().devices().at(ref->as<model::Device>()).line;
	if (line && line != m_target)
	{
		m_target = line;
		Q_EMIT targetChanged();
	}
}

void CatalogPanelVM::onEntriesChanged()
{
	if (!m_currentKey.isEmpty() && !m_catalog->entry(m_currentKey))
	{
		m_currentKey.clear();
		Q_EMIT currentChanged();
		Q_EMIT targetChanged();
	}
	Q_EMIT statusChanged();
}

void CatalogPanelVM::activate()
{
	m_catalog->ensureScanned();
	Q_EMIT statusChanged();
}

void CatalogPanelVM::rescan()
{
	m_catalog->rescan();
	Q_EMIT statusChanged();
}

bool CatalogPanelVM::importFiles(const QStringList& paths)
{
	QStringList errors;
	for (const QString& path : paths)
	{
		const QUrl url(path);
		const QString local = url.isLocalFile() ? url.toLocalFile() : path;
		const auto imported = m_catalog->importFile(local);
		if (!imported)
			errors.append(errorText(imported.error()));
	}
	if (!errors.isEmpty())
		setError(errors.join(u'\n'));
	else
		clearError();
	Q_EMIT statusChanged();
	return errors.isEmpty();
}

void CatalogPanelVM::setCurrentRow(int row)
{
	setCurrentKey(keyAt(row));
}

void CatalogPanelVM::setCurrentKey(const QString& key)
{
	if (key == m_currentKey)
		return;
	m_currentKey = key;
	Q_EMIT currentChanged();
	Q_EMIT targetChanged();
}

QString CatalogPanelVM::keyAt(int row) const
{
	return m_filtered->index(row, 0).data(KeyedTableModel::KeyRole).toString();
}

QString CatalogPanelVM::addToLine(const QString& key, const QString& lineOrDeviceRef)
{
	std::optional<model::Id<model::Line>> line;
	if (lineOrDeviceRef.isEmpty())
	{
		line = targetLine();
		if (!line)
		{
			setError(tr("Select a line (or a device on it) in Topology first."));
			return {};
		}
	}
	else
	{
		const auto ref = parseRef(lineOrDeviceRef);
		if (!ref)
			return {};
		if (ref->is<model::Line>())
			line = ref->as<model::Line>();
		else if (ref->is<model::Device>())
			line = project().devices().at(ref->as<model::Device>()).line;
		if (!line)
		{
			setError(tr("Products can only be added to a line."));
			return {};
		}
	}
	const QString productKey = key.isEmpty() ? m_currentKey : key;
	if (!addToLine(productKey, *line))
		return {};
	return selection()->currentText();
}

bool CatalogPanelVM::addToLine(const QString& key, model::Id<model::Line> line)
{
	const CatalogEntry* entry = m_catalog->entry(key);
	if (!entry)
	{
		setError(key.isEmpty() ? tr("Select a product in the catalog first.") : tr("Unknown catalog product."));
		return false;
	}
	if (!project().lines().contains(line))
	{
		setError(tr("The line no longer exists."));
		return false;
	}
	const auto number = nextFreeDeviceNumber(project(), line);
	if (!number)
	{
		setError(tr("All device numbers of this line are in use."));
		return false;
	}
	const auto id = check(model::addDevice(project(), line, *number, entry->productText, entry->productId, entry->programId));
	if (!id)
		return false;
	m_target = line;
	selection()->select(*id, origin());
	Q_EMIT targetChanged();
	return true;
}

} // namespace kestrel::viewmodels
