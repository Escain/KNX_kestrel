#include "kestrel/viewmodels/PanelVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/ActionRegistry.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"
#include "kestrel/viewmodels/ShadowTreeModel.hpp"

#include <cassert>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

PanelVM::PanelVM(const Services& services, const QString& origin, QObject* parent)
	: QObject(parent), m_services(services), m_origin(origin)
{
	assert(services.session && services.selection && services.actions);
}

model::Project& PanelVM::project()
{
	return m_services.session->project();
}

const model::Project& PanelVM::project() const
{
	return m_services.session->project();
}

void PanelVM::clearError()
{
	if (!m_lastError.isEmpty())
	{
		m_lastError.clear();
		Q_EMIT lastErrorChanged();
	}
}

void PanelVM::setError(const Error& error)
{
	setError(errorText(error));
}

void PanelVM::setError(const QString& message)
{
	if (message != m_lastError)
	{
		m_lastError = message;
		Q_EMIT lastErrorChanged();
	}
	if (!message.isEmpty())
	{
		m_services.session->reportError(message);
	}
}

bool PanelVM::check(const Status& status)
{
	if (!status)
	{
		setError(status.error());
		return false;
	}
	clearError();
	return true;
}

std::optional<model::EntityRef> PanelVM::parseRef(const QString& ref)
{
	const auto parsed = model::EntityRef::parse(ref);
	if (!parsed || !project().contains(*parsed))
	{
		setError(tr("Unknown object: %1").arg(ref));
		return std::nullopt;
	}
	return parsed;
}

bool PanelVM::rename(const QString& ref, const QString& name)
{
	const auto parsed = parseRef(ref);
	return parsed && check(model::rename(project(), *parsed, name.trimmed()));
}

bool PanelVM::remove(const QString& ref)
{
	const auto parsed = parseRef(ref);
	return parsed && check(removeEntity(project(), *parsed));
}

void PanelVM::select(const QString& ref)
{
	m_services.selection->selectRef(ref, m_origin);
}

// --- TreePanelVM -----------------------------------------------------------------------------------------------

TreePanelVM::TreePanelVM(const Services& services, const QString& origin, QObject* parent)
	: PanelVM(services, origin, parent)
{
	connect(selection(), &SelectionService::selectionChanged, this, &TreePanelVM::onSelectionChanged);
	connect(selection(), &SelectionService::renameRequested, this,
		[this](const QString& ref)
		{
			if (selection()->origin() == this->origin())
			{
				if (const QModelIndex index = indexOfRef(ref); index.isValid())
					Q_EMIT editRequested(index);
			}
		});
}

QAbstractItemModel* TreePanelVM::model() const
{
	return m_model;
}

void TreePanelVM::setModel(ShadowTreeModel* model)
{
	assert(!m_model && model);
	m_model = model;
	m_model->setParent(this);
}

void TreePanelVM::selectIndex(const QModelIndex& index)
{
	if (!index.isValid())
	{
		return;
	}
	selection()->select(m_model->refAt(index), origin());
}

void TreePanelVM::activateIndex(const QModelIndex& index)
{
	if (index.isValid())
	{
		selection()->activate(m_model->refAt(index), origin());
	}
}

void TreePanelVM::contextMenuAt(const QModelIndex& index)
{
	if (index.isValid())
	{
		selection()->select(m_model->refAt(index), origin());
	}
	else
	{
		selection()->setSelection({}, std::nullopt, origin());
	}
	actions()->requestContextMenu(contextActions(m_model->refAt(index)));
}

QModelIndex TreePanelVM::indexOfRef(const QString& ref) const
{
	return m_model->indexOfRef(ref);
}

QString TreePanelVM::refAt(const QModelIndex& index) const
{
	return m_model->refString(index);
}

void TreePanelVM::onSelectionChanged()
{
	if (selection()->origin() == origin() || !selection()->current())
	{
		return;
	}
	if (const QModelIndex index = m_model->indexOf(*selection()->current()); index.isValid())
	{
		Q_EMIT revealRequested(index);
	}
}

} // namespace kestrel::viewmodels
