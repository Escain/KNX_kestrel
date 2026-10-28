#pragma once

// Reading a manufacturer product file (.knxprod: a ZIP of KNX project-schema XML, any
// http://knx.org/xml/project/NN namespace). Layout:
//   M-XXXX/Hardware.xml          hardware, products, Hardware2Program → application program refs (required)
//   M-XXXX/Catalog.xml           catalog sections and items
//   M-XXXX/M-XXXX_A-....xml      application programs (one or more)
//   M-XXXX/Baggages.xml, M-XXXX/Baggages/..., knx_master.xml (noted, not parsed)
//
// Unknown elements and attributes are ignored. Recoverable problems (bad numbers, dangling references,
// missing Catalog.xml, unsupported ModuleDefs) become warnings; broken XML, a foreign root element and a
// missing Hardware.xml are errors.

#include "kestrel/base/Bytes.hpp"
#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/Model.hpp"

#include <QByteArray>
#include <QString>

#include <map>
#include <ostream>
#include <vector>

namespace kestrel::catalog
{

/// Archive path → file content.
using PackageFiles = std::map<QString, QByteArray>;

struct LoadedDatabase
{
	ProductDatabase database;
	std::vector<Diagnostic> warnings;

	friend bool operator==(const LoadedDatabase&, const LoadedDatabase&) = default;
};

std::ostream& operator<<(std::ostream& os, const LoadedDatabase& value);

/// Parses the files of an unpacked product file (entries outside M-XXXX/ and knx_master.xml are ignored).
Result<LoadedDatabase> parsePackage(const PackageFiles& files);

/// Opens a .knxprod held in memory (through kestrel::archive) and parses it.
Result<LoadedDatabase> loadKnxprod(Bytes data);

/// Reads a .knxprod from disk.
Result<LoadedDatabase> loadKnxprodFile(const QString& path);

// --- listing (kestrel-cli catalog inspect, catalog browser) -------------------------------------------------

struct ProgramSummary
{
	QString id;
	QString name;
	QString maskVersion;
	unsigned applicationNumber = 0;
	unsigned applicationVersion = 0;
	bool available = false; ///< the application program is part of this file

	friend bool operator==(const ProgramSummary&, const ProgramSummary&) = default;
};

struct ProductSummary
{
	QString productId;
	QString text;
	QString orderNumber;
	QString hardwareId;
	QString hardwareName;
	std::vector<ProgramSummary> programs; ///< via the hardware's Hardware2Program refs

	friend bool operator==(const ProductSummary&, const ProductSummary&) = default;
};

std::ostream& operator<<(std::ostream& os, const ProgramSummary& value);
std::ostream& operator<<(std::ostream& os, const ProductSummary& value);

/// Every product with its application programs, texts in `language` (with fallback).
std::vector<ProductSummary> listProducts(const ProductDatabase& database, const QString& language = {});

} // namespace kestrel::catalog
