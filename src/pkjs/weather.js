
// ---- OpenMeteo weather code → human-readable label ----

var Languages = require('./languages');

function getCondition(code, lang) {
  var L = Languages.getLang(lang);
  return L.weather[code] || Languages.getLang(0).weather[code] || 'WX' + code;
}

var OPENMETEO_BASE = 'https://api.open-meteo.com/v1/forecast';
var FETCH_TIMEOUT_MS = 15000;
// Slightly shorter than the watch's 30-min heartbeat interval, so a normally
// scheduled heartbeat always refreshes weather, but a quick menu open/close in
// between does not race the timer and trigger a redundant fetch.
var WEATHER_CACHE_MAX_AGE_MS = 25 * 60 * 1000;
var WEATHER_CACHE_DISPLAY_MAX_AGE_MS = 6 * 60 * 60 * 1000;

function isFreshWeather(weather) {
  return !!(weather && weather.fetchedAt &&
    (Date.now() - weather.fetchedAt) <= WEATHER_CACHE_MAX_AGE_MS);
}

function isDisplayableWeather(weather) {
  return !!(weather && weather.fetchedAt &&
    (Date.now() - weather.fetchedAt) <= WEATHER_CACHE_DISPLAY_MAX_AGE_MS);
}

function clearWeather() {
  localStorage.removeItem('halcyonWeather');
}

function fetchWeather(lat, lng, callback, errback) {
  var url = OPENMETEO_BASE +
    '?latitude=' + lat +
    '&longitude=' + lng +
    '&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,precipitation,uv_index,dew_point_2m' +
    '&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max' +
    '&forecast_days=1' +
    '&timezone=auto';

  console.log(url);

  function fail(reason) {
    console.log('Weather fetch failed: ' + reason);
    if (errback) errback(reason);
  }

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.timeout = FETCH_TIMEOUT_MS;
  xhr.onload = function () {
    if (xhr.status !== 200) {
      fail('status ' + xhr.status);
      return;
    }
    try {
      var data = JSON.parse(xhr.responseText);
      var cur = data.current;
      var daily = data.daily;
      var pop = daily.precipitation_probability_max ||
        daily.precipitation_probability_mean ||
        daily.precipitation_probability_min ||
        [0];

      var weather = {
        temp: cur.temperature_2m,
        tempHi: daily.temperature_2m_max[0],
        tempLo: daily.temperature_2m_min[0],
        code: cur.weather_code,
        codeDay: daily.weather_code[0],
        hum: cur.relative_humidity_2m,
        wind: cur.wind_speed_10m,
        wind_dir: cur.wind_direction_10m,
        uv: cur.uv_index,
        rain: cur.precipitation,
        pop: pop[0],
        dew: cur.dew_point_2m,
        fetchedAt: Date.now()
      };

      localStorage.setItem('halcyonWeather', JSON.stringify(weather));
      console.log('Weather fetched: ' + JSON.stringify(weather));

      if (callback) callback(weather);
    } catch (e) {
      fail('parse error: ' + e);
    }
  };
  xhr.onerror = function () { fail('network error'); };
  xhr.ontimeout = function () { fail('timeout after ' + FETCH_TIMEOUT_MS + 'ms'); };
  xhr.send();
}

function restoreWeather() {
  var saved = localStorage.getItem('halcyonWeather');
  if (saved) {
    try {
      var weather = JSON.parse(saved);
      if (isDisplayableWeather(weather)) return weather;
      clearWeather();
      return null;
    } catch (e) {
      clearWeather();
      return null;
    }
  }
  return null;
}

function toF(celsius) {
  return Math.round(celsius * 9 / 5 + 32);
}

function toMPH(kmh) {
  return Math.round(kmh * 0.621371);
}

function toInch(mm) {
  return (mm * 0.03937).toFixed(2);
}

function getCardinal(degrees, lang) {
  var index = Math.round(degrees / 45) % 8;
  return Languages.getLang(lang).cardinals[index];
}

module.exports = {
  fetch: fetchWeather,
  restore: restoreWeather,
  isFresh: isFreshWeather,
  isDisplayable: isDisplayableWeather,
  clear: clearWeather,
  toF: toF,
  toMPH: toMPH,
  toInch: toInch,
  getCardinal: getCardinal,
  getCondition: getCondition
};
