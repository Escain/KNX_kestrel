#pragma once

// The product index (specs/07-catalog.md §6): manufacturers' products with the official download of their
// product file. Kestrel ships this list, never the files (ADR-009); a file is fetched from the manufacturer when
// the user asks for it, then imported like any other .knxprod.

#include "kestrel/base/Result.hpp"

#include <QByteArray>
#include <QString>

#include <vector>

namespace kestrel::catalog
{

struct IndexedProduct
{
	QString manufacturer;   ///< "Merten (Schneider Electric)"
	QString manufacturerId; ///< of the product file, "M-0064"
	QString orderNumber;
	QString product;        ///< the product's text in the file
	QString category;       ///< "switch actuator", "push button", ...
	QString url;            ///< direct download (a .knxprod or a zip holding one)
	QString page;           ///< the manufacturer's page for people (may be empty)
	QString format;         ///< "zip" or "knxprod"
	qint64 size = 0;        ///< of the download when the entry was checked
	QString sha256;         ///< of the download when the entry was checked (the file may be updated since)
	QString applicationId;
	QString mask;           ///< "MV-07B0"
	QString verified;       ///< date the download was checked

	friend bool operator==(const IndexedProduct&, const IndexedProduct&) = default;
};

/// {"version": 1, "products": [{...}, ...]}. Entries without url, manufacturer or product are errors.
Result<std::vector<IndexedProduct>> parseProductIndex(const QByteArray& json);

/// The index built into Kestrel (resource :/kestrel/catalog/product-index.json); empty if it cannot be read.
const std::vector<IndexedProduct>& builtinProductIndex();

/// Positions of the products matching every word of `query` (case-insensitive) in manufacturer, product, order
/// number or category, best first. Order numbers also match without punctuation ("670804" finds "MTN670804"),
/// and a word of five letters or more also matches a word one edit away ("mergten" finds "Merten").
/// An empty query matches everything, in index order.
std::vector<std::size_t> searchProductIndex(const std::vector<IndexedProduct>& index, const QString& query);

struct ExtractedFile
{
	QString name; ///< file name of the .knxprod
	QByteArray data;
};

/// The product files of a download: the download itself if it is a .knxprod (a zip with M-XXXX/ folders or
/// knx_master.xml), else the .knxprod entries of a zip, looking one level into nested zips.
Result<std::vector<ExtractedFile>> extractProductFiles(const QByteArray& download, const QString& downloadName);

} // namespace kestrel::catalog
