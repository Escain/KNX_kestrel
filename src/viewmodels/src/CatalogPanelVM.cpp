#include "kestrel/viewmodels/CatalogPanelVM.hpp"

#include "kestrel/app/ProductFetcher.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <QDir>
#include <QMimeData>
#include <QRegularExpression>
#include <QTemporaryDir>
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

// --- ProductIndexModel ----------------------------------------------------------------------------------------

namespace
{

QString foldedOrder(QString text)
{
	static const QRegularExpression punctuation(u"[^A-Za-z0-9]"_s);
	return text.remove(punctuation).toLower();
}

} // namespace

ProductIndexModel::ProductIndexModel(CatalogService* catalog, QObject* parent)
	: QAbstractListModel(parent), m_catalog(catalog), m_index(catalog::builtinProductIndex())
{
	setQuery({});
	connect(m_catalog, &CatalogService::entriesChanged, this, &ProductIndexModel::refresh);
}

int ProductIndexModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ProductIndexModel::data(const QModelIndex& index, int role) const
{
	const catalog::IndexedProduct* p = productAt(index.row());
	if (!p)
		return {};
	const std::size_t position = m_rows[static_cast<std::size_t>(index.row())];
	switch (role)
	{
	case Qt::DisplayRole:
	case ProductRole: return p->product;
	case ManufacturerRole: return p->manufacturer;
	case OrderNumberRole: return p->orderNumber;
	case CategoryRole: return p->category;
	case MaskRole: return p->mask;
	case DeviceDownloadRole: return p->mask == u"MV-07B0" || p->mask == u"MV-0021";
	case StateRole:
		if (m_downloading.contains(position))
			return u"downloading"_s;
		if (isImported(*p))
			return u"imported"_s;
		return m_failed.contains(position) ? u"failed"_s : u"available"_s;
	case ProgressRole: return m_downloading.contains(position) ? m_downloading.at(position) : 0.0;
	default: return {};
	}
}

QHash<int, QByteArray> ProductIndexModel::roleNames() const
{
	return {{ManufacturerRole, "manufacturer"}, {ProductRole, "product"}, {OrderNumberRole, "orderNumber"},
		{CategoryRole, "category"}, {MaskRole, "mask"}, {DeviceDownloadRole, "deviceDownload"},
		{StateRole, "state"}, {ProgressRole, "progress"}};
}

void ProductIndexModel::setIndex(std::vector<catalog::IndexedProduct> index)
{
	beginResetModel();
	m_index = std::move(index);
	m_downloading.clear();
	m_failed.clear();
	m_rows.clear();
	endResetModel();
	setQuery({});
}

void ProductIndexModel::setQuery(const QString& query)
{
	auto rows = catalog::searchProductIndex(m_index, query);
	if (rows == m_rows)
		return;
	beginResetModel();
	m_rows = std::move(rows);
	endResetModel();
}

const catalog::IndexedProduct* ProductIndexModel::productAt(int row) const
{
	const auto position = positionAt(row);
	return position ? &m_index[*position] : nullptr;
}

std::optional<std::size_t> ProductIndexModel::positionAt(int row) const
{
	if (row < 0 || static_cast<std::size_t>(row) >= m_rows.size())
		return std::nullopt;
	return m_rows[static_cast<std::size_t>(row)];
}

bool ProductIndexModel::isImported(const catalog::IndexedProduct& product) const
{
	const QString order = foldedOrder(product.orderNumber);
	for (const CatalogEntry& entry : m_catalog->entries())
	{
		if (entry.manufacturerId == product.manufacturerId && foldedOrder(entry.orderNumber) == order)
			return true;
	}
	return false;
}

void ProductIndexModel::changed(std::size_t position)
{
	for (std::size_t row = 0; row < m_rows.size(); ++row)
	{
		if (m_rows[row] == position)
			Q_EMIT dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)), {StateRole, ProgressRole});
	}
}

void ProductIndexModel::setDownloading(std::size_t position, double progress)
{
	m_failed.erase(position);
	m_downloading[position] = progress;
	changed(position);
}

void ProductIndexModel::setFailed(std::size_t position)
{
	m_downloading.erase(position);
	m_failed.insert(position);
	changed(position);
}

void ProductIndexModel::clearState(std::size_t position)
{
	m_downloading.erase(position);
	m_failed.erase(position);
	changed(position);
}

void ProductIndexModel::refresh()
{
	if (!m_rows.empty())
		Q_EMIT dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {StateRole});
}

// --- CatalogPanelVM ---------------------------------------------------------------------------------------------

CatalogPanelVM::CatalogPanelVM(const Services& services, CatalogService* catalog, QObject* parent)
	: PanelVM(services, u"catalog"_s, parent)
	, m_catalog(catalog)
	, m_index(new ProductIndexModel(catalog, this))
	, m_fetcher(new app::ProductFetcher(this))
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
	m_index->setQuery(filter);
	Q_EMIT filterChanged();
	Q_EMIT indexChanged();
}

QAbstractItemModel* CatalogPanelVM::indexModel() const
{
	return m_index;
}

int CatalogPanelVM::indexCount() const
{
	return m_index->rowCount();
}

bool CatalogPanelVM::isDownloading() const
{
	return m_downloadPosition.has_value();
}

void CatalogPanelVM::setProductIndex(std::vector<catalog::IndexedProduct> index)
{
	cancelDownload();
	m_index->setIndex(std::move(index));
	m_index->setQuery(m_filter);
	Q_EMIT indexChanged();
}

void CatalogPanelVM::cancelDownload()
{
	m_fetcher->cancel();
}

void CatalogPanelVM::downloadIndexed(int row)
{
	const auto position = m_index->positionAt(row);
	if (!position)
		return;
	if (m_downloadPosition)
	{
		setError(tr("Another product file is being downloaded."));
		return;
	}
	const catalog::IndexedProduct product = *m_index->productAt(row);
	m_downloadPosition = position;
	m_index->setDownloading(*position, 0.0);
	Q_EMIT indexChanged();
	m_fetcher->fetch(product,
		[this, product, position = *position](Result<app::FetchedProduct> fetched)
		{
			m_downloadPosition.reset();
			auto fail = [&](const QString& message)
			{
				m_index->setFailed(position);
				setError(message);
				Q_EMIT indexChanged();
				Q_EMIT downloadFinished(false, message);
			};
			if (!fetched)
			{
				fail(tr("%1 %2: %3").arg(product.manufacturer, product.orderNumber,
					QString::fromStdString(fetched.error().message)));
				return;
			}
			// The catalog copies each file into its folder and indexes it in the background.
			QTemporaryDir staging;
			if (!staging.isValid())
			{
				fail(tr("Cannot create a temporary folder for the download."));
				return;
			}
			QStringList imported;
			for (const catalog::ExtractedFile& file : fetched->files)
			{
				const QString path = staging.filePath(file.name);
				QFile out(path);
				if (!out.open(QIODevice::WriteOnly) || out.write(file.data) != file.data.size())
				{
					fail(tr("Cannot write %1.").arg(path));
					return;
				}
				out.close();
				auto name = m_catalog->importFile(path);
				if (!name)
				{
					fail(QString::fromStdString(name.error().message));
					return;
				}
				imported.append(*name);
			}
			m_index->clearState(position);
			clearError();
			QString message = tr("Downloaded %1 %2 (%3).").arg(product.manufacturer, product.orderNumber, imported.join(u", "_s));
			if (!fetched->matchesIndex)
				message += u' ' + tr("The manufacturer has updated the file since the index was made.");
			Q_EMIT indexChanged();
			Q_EMIT statusChanged();
			Q_EMIT downloadFinished(true, message);
		},
		[this, position = *position](qint64 received, qint64 total)
		{
			if (total > 0)
				m_index->setDownloading(position, static_cast<double>(received) / static_cast<double>(total));
		});
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
