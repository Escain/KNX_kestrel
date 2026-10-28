#include "kestrel/shell/Style.hpp"

#include <QApplication>
#include <QQuickStyle>

using namespace Qt::StringLiterals;

namespace kestrel::shell
{

void initializeStyle()
{
	QApplication::setStyle(u"Fusion"_s);
	QQuickStyle::setStyle(u"Fusion"_s);
}

} // namespace kestrel::shell
