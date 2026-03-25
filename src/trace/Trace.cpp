#include "trace/Trace.h"

#include <array>
#include <iomanip>
#include <sstream>

// Disable shadow warnings for OpenTelemetry headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/semconv/service_attributes.h>
#include <opentelemetry/trace/provider.h>

#pragma GCC diagnostic pop

namespace otlp = opentelemetry::exporter::otlp;
namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource = opentelemetry::sdk::resource;

namespace creatures {

// Global tracer instance
std::shared_ptr<Tracer> tracer;

// --- Tracer ---

Tracer::Tracer() : initialized_(false) {}

void Tracer::initialize(const std::string& serviceName,
                        const std::string& serviceVersion,
                        const std::string& honeycombApiKey,
                        const std::string& honeycombDataset) {

    auto resourceAttrs = resource::ResourceAttributes{
        {opentelemetry::semconv::service::kServiceName, serviceName},
        {opentelemetry::semconv::service::kServiceVersion, serviceVersion}};
    auto res = resource::Resource::Create(resourceAttrs);

    otlp::OtlpHttpExporterOptions opts;
    if (!honeycombApiKey.empty()) {
        opts.url = "https://api.honeycomb.io/v1/traces";
        opts.http_headers.insert({"x-honeycomb-team", honeycombApiKey});
        opts.http_headers.insert({"x-honeycomb-dataset", honeycombDataset});
    } else {
        opts.url = "http://localhost:4318/v1/traces";
    }

    auto exporter = otlp::OtlpHttpExporterFactory::Create(opts);
    auto processor = trace_sdk::BatchSpanProcessorFactory::Create(
        std::move(exporter), trace_sdk::BatchSpanProcessorOptions{});

    auto providerUnique = trace_sdk::TracerProviderFactory::Create(
        std::move(processor), res);
    std::shared_ptr<trace_api::TracerProvider> providerStd = std::move(providerUnique);
    auto providerShared = opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(providerStd);

    trace_api::Provider::SetTracerProvider(providerShared);
    tracer_ = trace_api::Provider::GetTracerProvider()->GetTracer(
        serviceName, serviceVersion);

    initialized_ = true;
    info("Tracing initialized (service: {}, version: {})", serviceName, serviceVersion);
    if (!honeycombApiKey.empty()) {
        info("  Exporting to Honeycomb dataset: {}", honeycombDataset);
    } else {
        info("  Exporting to localhost:4318 (no Honeycomb key set)");
    }
}

std::shared_ptr<Span> Tracer::startSpan(const std::string& operationName) {
    if (!initialized_ || !tracer_) return nullptr;

    auto span = tracer_->StartSpan(operationName);
    return std::make_shared<Span>(span);
}

std::shared_ptr<Span> Tracer::startChildSpan(const std::string& operationName,
                                              std::shared_ptr<Span> parent) {
    if (!initialized_ || !tracer_) return nullptr;
    if (!parent) return startSpan(operationName);

    auto options = trace_api::StartSpanOptions{};
    options.parent = parent->getContext();
    auto span = tracer_->StartSpan(operationName, options);
    return std::make_shared<Span>(span);
}

// --- Span ---

Span::Span(opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span)
    : span_(std::move(span)), statusSet_(false) {
    // Capture the context with this span active, for parent-child propagation
    context_ = opentelemetry::context::RuntimeContext::GetCurrent();
    context_ = context_.SetValue(
        opentelemetry::trace::kSpanKey,
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>(span_));
}

Span::~Span() {
    if (span_) {
        if (!statusSet_) {
            span_->SetStatus(trace_api::StatusCode::kOk);
        }
        span_->End();
    }
}

void Span::setAttribute(const std::string& key, const std::string& value) {
    if (span_) span_->SetAttribute(key, value);
}

void Span::setAttribute(const std::string& key, int64_t value) {
    if (span_) span_->SetAttribute(key, value);
}

void Span::setAttribute(const std::string& key, double value) {
    if (span_) span_->SetAttribute(key, value);
}

void Span::setAttribute(const std::string& key, bool value) {
    if (span_) span_->SetAttribute(key, value);
}

void Span::setSuccess() {
    if (span_) {
        span_->SetStatus(trace_api::StatusCode::kOk);
        statusSet_ = true;
    }
}

void Span::setError(const std::string& errorMessage) {
    if (span_) {
        span_->SetStatus(trace_api::StatusCode::kError, errorMessage);
        statusSet_ = true;
    }
}

void Span::recordException(const std::exception& ex) {
    if (span_) {
        span_->AddEvent("exception",
            {{"exception.message", std::string(ex.what())}});
        setError(ex.what());
    }
}

std::string Span::traceparent() const {
    if (!span_) return "";

    auto ctx = span_->GetContext();
    if (!ctx.IsValid()) return "";

    // Format: 00-{traceId}-{spanId}-{flags}
    // traceId = 32 hex chars, spanId = 16 hex chars
    auto traceId = ctx.trace_id();
    auto spanId = ctx.span_id();
    auto flags = ctx.trace_flags();

    std::ostringstream ss;
    ss << "00-";

    // Trace ID (16 bytes → 32 hex)
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<int>(traceId.Id()[i]);
    }
    ss << "-";

    // Span ID (8 bytes → 16 hex)
    for (int i = 0; i < 8; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<int>(spanId.Id()[i]);
    }
    ss << "-";

    // Flags (1 byte → 2 hex)
    ss << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<int>(flags.IsSampled() ? 0x01 : 0x00);

    return ss.str();
}

}  // namespace creatures
