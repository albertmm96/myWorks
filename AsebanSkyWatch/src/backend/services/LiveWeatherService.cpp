#include "LiveWeatherService.h"
#include "OpenWeatherFetcher.h"

LiveWeatherService::LiveWeatherService(OpenWeatherFetcher* fetcher, QObject* parent)
    : QObject(parent), fetcher_(fetcher)
{
    connect(fetcher_, &OpenWeatherFetcher::weatherReady,
        this, &LiveWeatherService::weatherReady);
    connect(fetcher_, &OpenWeatherFetcher::fetchError,
        this, &LiveWeatherService::serviceError);
}

void LiveWeatherService::requestWeather(double lat, double lon)
{
    if (!fetcher_) return;
    fetcher_->fetchCurrentWeather(lat, lon);
}