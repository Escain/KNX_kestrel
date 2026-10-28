#pragma once

// Project validation (specs/03-domain-model.md §5). Rules are pure functions over a Project.

#include "kestrel/model/Id.hpp"
#include "kestrel/model/Project.hpp"

#include <QString>
#include <QStringView>

#include <optional>
#include <ostream>
#include <vector>

namespace kestrel::validate
{

enum class Severity
{
	Error,
	Warning,
	Info,
};

const char* toString(Severity severity);
std::ostream& operator<<(std::ostream& os, Severity severity);

struct Diagnostic
{
	QString code;    ///< "TOP-001"
	Severity severity = Severity::Error;
	QString message; ///< constant English sentence per rule (also the translation key); details are the subjects
	std::vector<model::EntityRef> subjects;

	friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

std::ostream& operator<<(std::ostream& os, const Diagnostic& diagnostic);

class DiagnosticSink
{
public:
	void report(Diagnostic diagnostic) { m_diagnostics.push_back(std::move(diagnostic)); }
	const std::vector<Diagnostic>& diagnostics() const { return m_diagnostics; }
	std::vector<Diagnostic> take() { return std::move(m_diagnostics); }

private:
	std::vector<Diagnostic> m_diagnostics;
};

using Rule = void (*)(const model::Project&, DiagnosticSink&);

struct RuleInfo
{
	const char* code;
	Severity severity;
	Rule rule;
};

/// All rules, in code order.
const std::vector<RuleInfo>& rules();

void checkDuplicateIndividualAddresses(const model::Project& project, DiagnosticSink& sink); // TOP-001
void checkDeviceNumberZero(const model::Project& project, DiagnosticSink& sink);             // TOP-002
void checkDevicesWithoutAddress(const model::Project& project, DiagnosticSink& sink);        // TOP-003
void checkDuplicateGroupAddresses(const model::Project& project, DiagnosticSink& sink);      // GA-001
void checkGroupAddressesWithoutDpt(const model::Project& project, DiagnosticSink& sink);     // GA-002
void checkDptSizeMismatch(const model::Project& project, DiagnosticSink& sink);              // GA-003
void checkGroupAddressesWithoutLinks(const model::Project& project, DiagnosticSink& sink);   // GA-004
void checkGroupAddressesWithoutSender(const model::Project& project, DiagnosticSink& sink);  // GA-005
void checkMultipleSendingLinks(const model::Project& project, DiagnosticSink& sink);         // CO-001
void checkDevicesNotPlaced(const model::Project& project, DiagnosticSink& sink);             // BLD-001
void checkDuplicateAreaAndLineNumbers(const model::Project& project, DiagnosticSink& sink);  // TOP-004
void checkGroupAddressZero(const model::Project& project, DiagnosticSink& sink);             // GA-006
void checkGroupRangeConsistency(const model::Project& project, DiagnosticSink& sink);        // GA-007
void checkDuplicateLinks(const model::Project& project, DiagnosticSink& sink);               // CO-002
void checkDuplicateComObjectNumbers(const model::Project& project, DiagnosticSink& sink);    // CO-003

/// Size in bits of a DPT from its main number; accepts "DPST-9-1", "DPT-9", "9.001" and "9".
/// nullopt for empty, malformed or unknown main numbers.
std::optional<unsigned> dptSizeInBits(QStringView dpt);

/// Orders by severity (errors first), then code, then subjects, then message.
void sortDiagnostics(std::vector<Diagnostic>& diagnostics);

/// Runs every rule; result sorted with sortDiagnostics().
std::vector<Diagnostic> run(const model::Project& project);

} // namespace kestrel::validate
