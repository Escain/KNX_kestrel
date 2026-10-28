#include "kestrel/viewmodels/ProblemsPanelVM.hpp"

#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProblemsModel.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

ProblemsPanelVM::ProblemsPanelVM(const Services& services, QObject* parent)
	: PanelVM(services, u"problems"_s, parent), m_model(new ProblemsModel(services.session, this))
{
	connect(m_model, &ProblemsModel::countsChanged, this, &ProblemsPanelVM::countsChanged);
}

QAbstractItemModel* ProblemsPanelVM::model() const
{
	return m_model;
}

int ProblemsPanelVM::errorCount() const
{
	return m_model->errorCount();
}

int ProblemsPanelVM::warningCount() const
{
	return m_model->warningCount();
}

int ProblemsPanelVM::infoCount() const
{
	return m_model->infoCount();
}

QString ProblemsPanelVM::summary() const
{
	if (errorCount() + warningCount() + infoCount() == 0)
	{
		return tr("No problems");
	}
	QStringList parts;
	if (errorCount() > 0)
		parts.append(countText(errorCount(), "%1 error", "%1 errors"));
	if (warningCount() > 0)
		parts.append(countText(warningCount(), "%1 warning", "%1 warnings"));
	if (infoCount() > 0)
		parts.append(countText(infoCount(), "%1 message", "%1 messages"));
	return parts.join(u", "_s);
}

void ProblemsPanelVM::navigate(int row)
{
	const QString ref = m_model->data(m_model->index(row, 0), ProblemsModel::RefRole).toString();
	if (!ref.isEmpty())
	{
		selection()->selectRef(ref, origin());
	}
}

void ProblemsPanelVM::activate(int row)
{
	const QString ref = m_model->data(m_model->index(row, 0), ProblemsModel::RefRole).toString();
	if (!ref.isEmpty())
	{
		selection()->activateRef(ref, origin());
	}
}

} // namespace kestrel::viewmodels
