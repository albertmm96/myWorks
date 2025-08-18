#include "bridge.h"
#include <QDebug>

void Bridge::mouseMoved(double lat, double lon)
{
	qInfo() << "[MAP]" << "lat=" << lat << "lon=" << lon;
}
