// Writes the sample project used by the view-model tests into a folder (manual GUI checks, screenshots):
//   kestrel_make_sample_project <dir>

#include "SampleProject.hpp"

#include "kestrel/persist/Persist.hpp"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	if (argc != 2)
	{
		std::fprintf(stderr, "usage: kestrel_make_sample_project <dir>\n");
		return 2;
	}
	kestrel::model::Project project;
	kestrel::testing::buildSampleProject(project);
	const kestrel::Status status = kestrel::persist::save(project, QString::fromLocal8Bit(argv[1]));
	if (!status)
	{
		std::fprintf(stderr, "%s\n", status.error().message.c_str());
		return 1;
	}
	return 0;
}
