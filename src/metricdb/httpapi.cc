/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2011-2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <fnordmetric/metricdb/httpapi.h>
#include <fnordmetric/metricdb/metricrepository.h>
#include <fnordmetric/util/jsonoutputstream.h>
#include <fnordmetric/util/stringutil.h>

namespace fnordmetric {
namespace metricdb {

static const char kMetricsUrl[] = "/metrics";
static const char kMetricsUrlPrefix[] = "/metrics/";
static const char kQueryUrl[] = "/query";

HTTPAPI::HTTPAPI(MetricRepository* metric_repo) : metric_repo_(metric_repo) {}

bool HTTPAPI::handleHTTPRequest(
    http::HTTPRequest* request,
    http::HTTPResponse* response) {
  util::URI uri(request->getUrl());
  auto path = uri.path();
  fnord::util::StringUtil::stripTrailingSlashes(&path);

  // PATH: ^/metrics/?$
  if (path == kMetricsUrl) {
    switch (request->method()) {
      case http::HTTPRequest::M_GET:
        renderMetricList(request, response, &uri);
        return true;
      default:
        return false;
    }
  }

  // PATH: ^/metrics/.*
  if (path.compare(0, sizeof(kMetricsUrlPrefix) - 1, kMetricsUrlPrefix) == 0) {
    // PATH: ^/metrics/(.*)$
    switch (request->method()) {
      case http::HTTPRequest::M_GET:
        renderMetricSampleScan(request, response, &uri);
        return true;
      case http::HTTPRequest::M_POST:
        insertSample(request, response, &uri);
        return true;
      default:
        return false;
    }
  }

  // PATH: ^/query/?*
  if (path == kQueryUrl) {
    switch (request->method()) {
      case http::HTTPRequest::M_GET:
        return true;
      case http::HTTPRequest::M_POST:
        return true;
      default:
        return false;
    }
    return true;
  }

  return false;
}

void HTTPAPI::renderMetricList(
    http::HTTPRequest* request,
    http::HTTPResponse* response,
    util::URI* uri) {
  response->setStatus(http::kStatusOK);
  response->addHeader("Content-Type", "application/json; charset=utf-8");
  util::JSONOutputStream json(response->getBodyOutputStream());

  json.beginObject();
  json.addObjectEntry("metrics");
  json.beginArray();

  for (const auto& metric : metric_repo_->listMetrics()) {
    json.beginObject();
    json.addObjectEntry("key");
    json.addString(metric->key());
    json.endObject();
  }

  json.endArray();
  json.endObject();
}

void HTTPAPI::insertSample(
    http::HTTPRequest* request,
    http::HTTPResponse* response,
    util::URI* uri) {
  auto params = uri->queryParams();

  auto metric_key = uri->path().substr(sizeof(kMetricsUrlPrefix) - 1);
  if (metric_key.size() < 3) {
    response->addBody("error: invalid metric key: " + metric_key);
    response->setStatus(http::kStatusBadRequest);
    return;
  }

  std::string value_str;
  if (!util::URI::getParam(params, "value", &value_str)) {
    response->addBody("error: missing ?value=... parameter");
    response->setStatus(http::kStatusBadRequest);
    return;
  }

  Sample<double> sample;
  try {
    sample.value = std::stod(value_str);
  } catch (std::exception& e) {
    response->addBody("error: invalid value: " + value_str);
    response->setStatus(http::kStatusBadRequest);
    return;
  }

  auto metric = metric_repo_->findOrCreateMetric(metric_key);
  metric->addSample(sample);
  response->setStatus(http::kStatusCreated);
}

void HTTPAPI::renderMetricSampleScan(
    http::HTTPRequest* request,
    http::HTTPResponse* response,
    util::URI* uri) {
  auto metric_key = uri->path().substr(sizeof(kMetricsUrlPrefix) - 1);
  if (metric_key.size() < 3) {
    response->addBody("error: invalid metric key: " + metric_key);
    response->setStatus(http::kStatusBadRequest);
    return;
  }

  auto metric = metric_repo_->findMetric(metric_key);
  if (metric == nullptr) {
    response->addBody("metric not found: " + metric_key);
    response->setStatus(http::kStatusNotFound);
    return;
  }

  response->setStatus(http::kStatusOK);
  response->addHeader("Content-Type", "application/json; charset=utf-8");
  util::JSONOutputStream json(response->getBodyOutputStream());

  json.beginObject();
  json.addObjectEntry("samples");
  json.beginArray();

  metric->scanSamples(
      fnord::util::DateTime::epoch(),
      fnord::util::DateTime::now(),
      [&json] (MetricCursor* cursor) -> bool {
        json.beginObject();
        json.addObjectEntry("time");
        json.addInteger(cursor->time());
        json.endObject();
        return true;
      });

  json.endArray();
  json.endObject();
}

}
}