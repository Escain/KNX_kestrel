#pragma once

// Streaming parser for one KNX project-schema document of a product file (QXmlStreamReader, recursive descent).

#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/Model.hpp"

#include <QByteArray>
#include <QString>

#include <map>
#include <vector>

namespace kestrel::catalog::detail
{

struct ParseState
{
	ProductDatabase database;
	std::vector<Diagnostic> warnings;
	/// TranslationUnit RefId → its translations; distributed to the owners once every file is read.
	std::map<QString, Translations> unitTranslations;
};

/// Parses one document into `state`. Errors: broken XML, a root other than <KNX>, a foreign namespace.
Status parseDocument(const QString& path, const QByteArray& content, ParseState& state);

} // namespace kestrel::catalog::detail
