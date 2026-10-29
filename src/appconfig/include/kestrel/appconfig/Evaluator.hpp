#pragma once

// DynamicEvaluator: which ParameterRefs and ComObjectRefs of an application program are active for a set of
// parameter values. The semantics were checked byte for byte against a real ETS6 download (specs/09):
// - Values belong to the Parameter; all of its ParameterRefs share them.
// - `choose ParamRefId` only counts while that ParameterRef is itself active. A choose on a parameter that is
//   not shown selects nothing, even if its value would match. Iterated to a fixpoint. A parameter that no
//   ParameterRefRef shows anywhere is internal: its chooses always count (converted pre-ETS4 programs).
// - `when test="a b c"` matches any listed value. Tokens may start with <, <=, >, >=, != for numeric
//   comparisons. `when default="true"` applies when no other branch of the choose matched.

#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/Model.hpp"

#include <QSet>
#include <QString>
#include <QStringList>

#include <map>

namespace kestrel::appconfig
{

/// Parameter id → canonical value (as in the XML, e.g. "1", "1.0E+001", "text"). Missing = default.
using ParameterValues = std::map<QString, QString>;

struct Evaluation
{
	QStringList parameterRefs;  ///< active ParameterRefs, document order, unique
	QStringList comObjectRefs;  ///< active ComObjectRefs, document order, unique
	QStringList warnings;       ///< unsupported dynamic elements (Assign, Rename, ...)

	bool isParameterRefActive(const QString& refId) const { return m_parameterSet.contains(refId); }
	bool isComObjectRefActive(const QString& refId) const { return m_comObjectSet.contains(refId); }

private:
	friend Result<Evaluation> evaluate(const catalog::ApplicationProgram&, const ParameterValues&);
	QSet<QString> m_parameterSet;
	QSet<QString> m_comObjectSet;
};

/// The value a ParameterRef shows: the parameter's value in `values`, else the ref's own default, else the
/// parameter's default. Empty when the ref or its parameter is unknown.
QString valueOf(const catalog::ApplicationProgram& app, const ParameterValues& values, const QString& parameterRefId);

/// Does `value` satisfy a `when test` attribute?
bool matchesTest(const QString& value, const QString& test);

/// Errors: InvalidArgument (no fixpoint within 100 iterations: the conditions oscillate).
Result<Evaluation> evaluate(const catalog::ApplicationProgram& app, const ParameterValues& values);

} // namespace kestrel::appconfig
