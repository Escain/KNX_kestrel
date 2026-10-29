#include "kestrel/viewmodels/ConfigurationHooks.hpp"

#include "kestrel/appconfig/DownloadPlan.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QSet>

#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

Q_LOGGING_CATEGORY(lcHooks, "kestrel.viewmodels.configuration")

namespace kestrel::viewmodels
{

std::vector<catalog::EffectiveComObject> allEffectiveComObjects(
	const catalog::ApplicationProgram& program, const model::Device& device, const QString& language)
{
	(void)device;
	return program.effectiveComObjects(language);
}

appconfig::ParameterValues parameterValues(const catalog::ApplicationProgram& program, const model::Device& device)
{
	appconfig::ParameterValues values;
	for (const auto& [refId, value] : device.params)
	{
		if (const catalog::ParameterRef* ref = program.findParameterRef(refId))
			values[ref->refId] = value;
	}
	return values;
}

std::vector<catalog::EffectiveComObject> activeComObjects(
	const catalog::ApplicationProgram& program, const model::Device& device, const QString& language)
{
	std::vector<catalog::EffectiveComObject> all = program.effectiveComObjects(language);
	const auto evaluation = appconfig::evaluate(program, parameterValues(program, device));
	if (!evaluation)
		return all;
	// Document order decides which of several active refs with one number is shown.
	QHash<QString, qsizetype> order;
	for (qsizetype i = 0; i < evaluation->comObjectRefs.size(); ++i)
		order.insert(evaluation->comObjectRefs.at(i), i);
	std::map<unsigned, const catalog::EffectiveComObject*> byNumber;
	for (const catalog::EffectiveComObject& object : all)
	{
		if (!evaluation->isComObjectRefActive(object.refId))
			continue;
		auto [it, inserted] = byNumber.emplace(object.number, &object);
		if (!inserted && order.value(object.refId) < order.value(it->second->refId))
			it->second = &object;
	}
	std::vector<catalog::EffectiveComObject> result;
	result.reserve(byNumber.size());
	for (const auto& [number, object] : byNumber)
		result.push_back(*object);
	return result;
}

DownloadPlanner catalogDownloadPlanner(CatalogService* catalog)
{
	return [catalog](const model::Project& project, model::Id<model::Device> device) -> Result<download::Plan>
	{
		const model::Device* entity = project.devices().find(device);
		if (!entity)
			return makeError(ErrorCode::NotFound, "the device no longer exists");
		const QString programId = entity->appProgramRef;
		const auto database = catalog->databaseForProgram(programId);
		if (!database)
		{
			return makeError(ErrorCode::NotFound,
				QObject::tr("The application program %1 is not in the catalog.").arg(programId).toStdString());
		}
		const catalog::ApplicationProgram* program = database->database.findApplicationProgram(programId);
		if (!program)
			return makeError(ErrorCode::NotFound, QObject::tr("The application program %1 is missing.").arg(programId).toStdString());
		KESTREL_TRY(appconfig::DevicePlan plan, appconfig::planDeviceDownload(database->database, *program, project, device));
		for (const QString& warning : plan.warnings)
			qCInfo(lcHooks) << "download plan of" << device.toString() << ":" << warning;
		return std::move(plan.plan);
	};
}

namespace
{

class PlanWriter
{
public:
	void u8(std::uint8_t v) { m_data.append(static_cast<char>(v)); }
	void u32(std::uint32_t v)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
			u8(static_cast<std::uint8_t>(v >> shift));
	}
	void flag(bool v) { u8(v ? 1 : 0); }
	void bytes(const Bytes& data)
	{
		u32(static_cast<std::uint32_t>(data.size()));
		m_data.append(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
	}
	const QByteArray& data() const { return m_data; }

private:
	QByteArray m_data;
};

} // namespace

QString planHash(const download::Plan& plan)
{
	using namespace download;
	PlanWriter w;
	for (const Step& step : plan.steps)
	{
		w.u8(static_cast<std::uint8_t>(step.index()));
		std::visit(
			[&w](const auto& s)
			{
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, Authorize>)
					w.u32(s.key);
				else if constexpr (std::is_same_v<T, Unload>)
				{
					w.u8(s.lsm);
					w.flag(s.tolerateError);
				}
				else if constexpr (std::is_same_v<T, Load> || std::is_same_v<T, LoadCompleted>)
					w.u8(s.lsm);
				else if constexpr (std::is_same_v<T, AllocRelSegment>)
				{
					w.u8(s.lsm);
					w.u32(s.size);
					w.u8(s.mode);
					w.u8(s.fill);
				}
				else if constexpr (std::is_same_v<T, WriteRelMem>)
				{
					w.u8(s.object);
					w.u32(s.offset);
					w.bytes(s.data);
					w.flag(s.verify);
				}
				else if constexpr (std::is_same_v<T, WriteProp>)
				{
					w.u8(s.object);
					w.u8(s.pid);
					w.bytes(s.data);
					w.flag(s.verify);
				}
				else if constexpr (std::is_same_v<T, CompareProp>)
				{
					w.u8(s.object);
					w.u8(s.pid);
					w.bytes(s.data);
				}
				else if constexpr (std::is_same_v<T, LoadImageProp>)
				{
					w.u8(s.object);
					w.u8(s.pid);
				}
				else if constexpr (std::is_same_v<T, AdditionalLoadControl>)
				{
					w.u8(s.lsm);
					w.bytes(s.data);
				}
				else if constexpr (std::is_same_v<T, WriteMem>)
				{
					w.u32(s.address);
					w.bytes(s.data);
					w.flag(s.verify);
				}
				else if constexpr (!std::is_same_v<T, Restart>)
				{
					// A step type added later: its printed form still identifies it.
					std::ostringstream os;
					os << Step(s);
					const std::string text = os.str();
					w.bytes(Bytes(text.begin(), text.end()));
				}
			},
			step);
	}
	return QString::fromLatin1(QCryptographicHash::hash(w.data(), QCryptographicHash::Sha256).toHex());
}

} // namespace kestrel::viewmodels
