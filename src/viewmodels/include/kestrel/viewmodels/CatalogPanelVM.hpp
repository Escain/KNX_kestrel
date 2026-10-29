#pragma once

// Catalog dock (FR-CAT-1/2/3; specs/08-gui.md §2): the imported products with a filter, import of
// .knxprod files and "Add to line", which instantiates the selected product on the line selected in Topology
// (one undo step: a model Device with productRef/appProgramRef, named after the product, next free number).
//
// The target line follows the selection: the selected line, or the line of the selected device. When the
// selection moves elsewhere (a group address, a building part) the last target is kept.

#include "kestrel/catalog/ProductIndex.hpp"
#include "kestrel/viewmodels/KeyedTableModel.hpp"
#include "kestrel/viewmodels/PanelVM.hpp"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include <map>
#include <optional>
#include <set>

namespace kestrel::app
{
class ProductFetcher;
}

namespace kestrel::viewmodels
{

class CatalogService;
struct CatalogEntry;

/// Products of the catalog, one row per (product, application program).
class CatalogProductModel : public KeyedTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		ProductColumn,
		OrderNumberColumn,
		ApplicationColumn,
		VersionColumn,
		ManufacturerColumn,
	};

	explicit CatalogProductModel(CatalogService* catalog, QObject* parent = nullptr);

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;
	QStringList mimeTypes() const override;
	QMimeData* mimeData(const QModelIndexList& indexes) const override;

	/// Re-reads the catalog entries (incremental).
	void sync();

private:
	CatalogService* m_catalog;
};

/// Filter (any column, case-insensitive) and sort (by product) over CatalogProductModel.
class CatalogFilterModel : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit CatalogFilterModel(QObject* parent = nullptr);

	/// Space-separated words; each must appear in some column.
	void setText(const QString& text);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
	QStringList m_words;
};

/// Products of the product index (catalog::builtinProductIndex()) that match the filter, with their download
/// state. Roles: manufacturer, product, orderNumber, category, mask, deviceDownload (Kestrel can download
/// to such a device), state ("available", "downloading", "imported", "failed"), progress (0..1).
class ProductIndexModel : public QAbstractListModel
{
	Q_OBJECT

public:
	enum Role
	{
		ManufacturerRole = Qt::UserRole + 1,
		ProductRole,
		OrderNumberRole,
		CategoryRole,
		MaskRole,
		DeviceDownloadRole,
		StateRole,
		ProgressRole,
	};

	explicit ProductIndexModel(CatalogService* catalog, QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	void setIndex(std::vector<catalog::IndexedProduct> index);
	void setQuery(const QString& query);
	const catalog::IndexedProduct* productAt(int row) const;
	/// Position in the index of a row.
	std::optional<std::size_t> positionAt(int row) const;

	void setDownloading(std::size_t position, double progress);
	void setFailed(std::size_t position);
	void clearState(std::size_t position);
	/// Re-checks which products are in the catalog.
	void refresh();

private:
	bool isImported(const catalog::IndexedProduct& product) const;
	void changed(std::size_t position);

	CatalogService* m_catalog;
	std::vector<catalog::IndexedProduct> m_index;
	std::vector<std::size_t> m_rows; ///< positions in m_index
	std::map<std::size_t, double> m_downloading;
	std::set<std::size_t> m_failed;
};

class CatalogPanelVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
	Q_PROPERTY(bool busy READ isBusy NOTIFY statusChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(QString problems READ problems NOTIFY statusChanged)
	Q_PROPERTY(int productCount READ productCount NOTIFY statusChanged)
	Q_PROPERTY(QString directory READ directory CONSTANT)
	Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentChanged)
	Q_PROPERTY(QString currentDetails READ currentDetails NOTIFY currentChanged)
	Q_PROPERTY(QString target READ targetText NOTIFY targetChanged)
	Q_PROPERTY(bool canAdd READ canAdd NOTIFY targetChanged)
	Q_PROPERTY(QAbstractItemModel* indexModel READ indexModel CONSTANT)
	Q_PROPERTY(int indexCount READ indexCount NOTIFY indexChanged)
	Q_PROPERTY(bool downloading READ isDownloading NOTIFY indexChanged)

public:
	/// MIME format of a dragged catalog entry (data: its key).
	static inline const QString kMimeType = QStringLiteral("application/x-kestrel-catalog-entry");

	CatalogPanelVM(const Services& services, CatalogService* catalog, QObject* parent = nullptr);

	CatalogService* catalog() const { return m_catalog; }
	QAbstractItemModel* model() const;
	CatalogProductModel* productModel() const { return m_products; }
	QString filter() const { return m_filter; }
	void setFilter(const QString& filter);
	bool isBusy() const;
	QString status() const;
	QString problems() const;
	int productCount() const;
	QString directory() const;
	QString currentKey() const { return m_currentKey; }
	QString currentDetails() const;
	/// "1.1 Ground floor line"; empty if no target line.
	QString targetText() const;
	/// A product is selected and a target line exists.
	bool canAdd() const;
	std::optional<model::Id<model::Line>> targetLine() const;

	/// The dock became visible: reads the catalog folder (once).
	Q_INVOKABLE void activate();
	Q_INVOKABLE void rescan();
	/// Imports .knxprod files (paths or file:// URLs). Returns false if any failed (lastError).
	Q_INVOKABLE bool importFiles(const QStringList& paths);
	/// Row of the (filtered) view.
	Q_INVOKABLE void setCurrentRow(int row);
	Q_INVOKABLE void setCurrentKey(const QString& key);
	/// Key at a row of the (filtered) view.
	Q_INVOKABLE QString keyAt(int row) const;
	/// Adds the product `key` (current if empty) to the line of `lineOrDeviceRef` (target line if empty).
	/// Returns the new device ref, or "" (lastError).
	Q_INVOKABLE QString addToLine(const QString& key = {}, const QString& lineOrDeviceRef = {});
	/// Same as addToLine() but for a product dropped on a topology entity.
	bool addToLine(const QString& key, model::Id<model::Line> line);

	QAbstractItemModel* indexModel() const;
	ProductIndexModel* productIndexModel() const { return m_index; }
	int indexCount() const;
	bool isDownloading() const;
	/// Downloads the product file of an index row from its manufacturer and imports it (one at a time).
	Q_INVOKABLE void downloadIndexed(int row);
	Q_INVOKABLE void cancelDownload();
	/// Replaces the built-in product index (tests).
	void setProductIndex(std::vector<catalog::IndexedProduct> index);

Q_SIGNALS:
	void filterChanged();
	void statusChanged();
	void currentChanged();
	void targetChanged();
	void indexChanged();
	/// A downloaded product file was handed to the catalog (or the download failed: ok false).
	void downloadFinished(bool ok, const QString& message);

private:
	void onSelectionChanged();
	void onEntriesChanged();

	CatalogService* m_catalog;
	ProductIndexModel* m_index;
	app::ProductFetcher* m_fetcher;
	std::optional<std::size_t> m_downloadPosition;
	CatalogProductModel* m_products;
	CatalogFilterModel* m_filtered;
	QString m_filter;
	QString m_currentKey;
	std::optional<model::Id<model::Line>> m_target;
};

/// Lowest free device number (1..255) on a line.
std::optional<unsigned> nextFreeDeviceNumber(const model::Project& project, model::Id<model::Line> line);

} // namespace kestrel::viewmodels
