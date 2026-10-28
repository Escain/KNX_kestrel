#include "kestrel/appconfig/Evaluator.hpp"

#include <cmath>

namespace kestrel::appconfig
{

using namespace Qt::StringLiterals;
using catalog::DynamicKind;
using catalog::DynamicNode;

QString valueOf(const catalog::ApplicationProgram& app, const ParameterValues& values, const QString& parameterRefId)
{
	const catalog::ParameterRef* ref = app.findParameterRef(parameterRefId);
	if (!ref)
	{
		return {};
	}
	if (const auto it = values.find(ref->refId); it != values.end())
	{
		return it->second;
	}
	if (ref->value)
	{
		return *ref->value;
	}
	const catalog::Parameter* parameter = app.findParameter(ref->refId);
	return parameter ? parameter->value : QString();
}

bool matchesTest(const QString& value, const QString& test)
{
	bool valueIsNumber = false;
	const double number = value.toDouble(&valueIsNumber);
	const QStringList tokens = test.split(u' ', Qt::SkipEmptyParts);
	for (const QString& token : tokens)
	{
		static const QString operators[] = {u"<="_s, u">="_s, u"!="_s, u"<"_s, u">"_s};
		QString op;
		for (const QString& candidate : operators)
		{
			if (token.startsWith(candidate))
			{
				op = candidate;
				break;
			}
		}
		const QString operand = token.mid(op.size());
		bool operandIsNumber = false;
		const double reference = operand.toDouble(&operandIsNumber);
		if (op.isEmpty())
		{
			if (valueIsNumber && operandIsNumber ? std::abs(number - reference) < 1e-9 : value == operand)
			{
				return true;
			}
			continue;
		}
		if (!valueIsNumber || !operandIsNumber)
		{
			continue;
		}
		if ((op == u"<" && number < reference) || (op == u"<=" && number <= reference)
			|| (op == u">" && number > reference) || (op == u">=" && number >= reference)
			|| (op == u"!=" && std::abs(number - reference) >= 1e-9))
		{
			return true;
		}
	}
	return false;
}

namespace
{

class Walker
{
public:
	Walker(const catalog::ApplicationProgram& app, const ParameterValues& values, const QSet<QString>* previous)
		: m_app(app), m_values(values), m_previous(previous)
	{
	}

	void walk(const DynamicNode& node)
	{
		for (const DynamicNode& child : node.children)
		{
			switch (child.kind)
			{
			case DynamicKind::ParameterRefRef:
				add(child.attribute(u"RefId"_s), m_result.parameterRefs, m_parameterSet);
				break;
			case DynamicKind::ComObjectRefRef:
				add(child.attribute(u"RefId"_s), m_result.comObjectRefs, m_comObjectSet);
				break;
			case DynamicKind::Choose: choose(child); break;
			case DynamicKind::When: break; // only reachable through choose
			case DynamicKind::Other:
				if (child.element == u"Assign" || child.element == u"Rename")
				{
					warnOnce(u"dynamic element %1 is not supported yet"_s.arg(child.element));
				}
				walk(child);
				break;
			case DynamicKind::Root:
			case DynamicKind::ChannelIndependentBlock:
			case DynamicKind::Channel:
			case DynamicKind::ParameterBlock: walk(child); break;
			}
		}
	}

	Evaluation take(QSet<QString>& parameters, QSet<QString>& comObjects)
	{
		parameters = m_parameterSet;
		comObjects = m_comObjectSet;
		return std::move(m_result);
	}

private:
	void add(const QString& id, QStringList& list, QSet<QString>& set)
	{
		if (!id.isEmpty() && !set.contains(id))
		{
			set.insert(id);
			list.append(id);
		}
	}

	void warnOnce(const QString& message)
	{
		if (!m_result.warnings.contains(message))
		{
			m_result.warnings.append(message);
		}
	}

	void choose(const DynamicNode& node)
	{
		const QString refId = node.attribute(u"ParamRefId"_s);
		if (m_previous && !m_previous->contains(refId))
		{
			return; // the controlling parameter is not shown: nothing below this choose is
		}
		const QString value = valueOf(m_app, m_values, refId);
		bool matched = false;
		for (const DynamicNode& branch : node.children)
		{
			if (branch.kind != DynamicKind::When)
			{
				continue;
			}
			const QString test = branch.attribute(u"test"_s);
			if (!test.isEmpty() && matchesTest(value, test))
			{
				matched = true;
				walk(branch);
			}
		}
		if (!matched)
		{
			for (const DynamicNode& branch : node.children)
			{
				if (branch.kind == DynamicKind::When && branch.attribute(u"default"_s) == u"true")
				{
					walk(branch);
				}
			}
		}
	}

	const catalog::ApplicationProgram& m_app;
	const ParameterValues& m_values;
	const QSet<QString>* m_previous;
	Evaluation m_result;
	QSet<QString> m_parameterSet;
	QSet<QString> m_comObjectSet;
};

} // namespace

Result<Evaluation> evaluate(const catalog::ApplicationProgram& app, const ParameterValues& values)
{
	// First pass: every choose counts. Then a choose only counts while its ParameterRef was active in the
	// previous pass, until the active set no longer changes.
	std::optional<QSet<QString>> previous;
	for (int iteration = 0; iteration < 100; ++iteration)
	{
		Walker walker(app, values, previous ? &*previous : nullptr);
		walker.walk(app.dynamic);
		QSet<QString> parameters;
		QSet<QString> comObjects;
		Evaluation evaluation = walker.take(parameters, comObjects);
		if (previous && parameters == *previous)
		{
			evaluation.m_parameterSet = std::move(parameters);
			evaluation.m_comObjectSet = std::move(comObjects);
			return evaluation;
		}
		previous = std::move(parameters);
	}
	return makeError(ErrorCode::InvalidArgument, "parameter visibility does not settle (conditions oscillate)");
}

} // namespace kestrel::appconfig
