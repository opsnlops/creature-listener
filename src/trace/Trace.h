#pragma once

#include <memory>
#include <string>

// Disable shadow warnings for OpenTelemetry headers (third-party code)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

#include <opentelemetry/context/context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>

#pragma GCC diagnostic pop

#include "util/namespace-stuffs.h"

namespace creatures {

// Forward declarations
class Span;

/// Lightweight tracing wrapper for creature-listener.
/// Initializes OpenTelemetry with OTLP HTTP export to Honeycomb,
/// creates spans, and generates W3C traceparent headers for
/// propagation to creature-server.
class Tracer {
public:
    Tracer();
    ~Tracer() = default;

    /// Initialize with Honeycomb (or local OTLP collector).
    /// If honeycombApiKey is empty, exports to localhost:4318.
    void initialize(const std::string& serviceName,
                    const std::string& serviceVersion,
                    const std::string& honeycombApiKey = "",
                    const std::string& honeycombDataset = "creature-listener");

    /// Create a root span (no parent).
    std::shared_ptr<Span> startSpan(const std::string& operationName);

    /// Create a child span under an existing parent.
    std::shared_ptr<Span> startChildSpan(const std::string& operationName,
                                          std::shared_ptr<Span> parent);

    [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;
    bool initialized_;
};

/// RAII span wrapper with attribute setting and traceparent generation.
class Span {
public:
    explicit Span(opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span);
    ~Span();

    // Non-copyable
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;

    /// Set attributes on the span.
    void setAttribute(const std::string& key, const std::string& value);
    void setAttribute(const std::string& key, int64_t value);
    void setAttribute(const std::string& key, double value);
    void setAttribute(const std::string& key, bool value);

    /// Mark the span as successful.
    void setSuccess();

    /// Mark the span as failed with an error message.
    void setError(const std::string& errorMessage);

    /// Record an exception.
    void recordException(const std::exception& ex);

    /// Generate a W3C traceparent header value from this span's context.
    /// Use this when making HTTP calls to creature-server so the trace
    /// continues on the server side.
    [[nodiscard]] std::string traceparent() const;

    /// Get the underlying OTel span (for child span creation).
    [[nodiscard]] opentelemetry::trace::Span* getSpan() const { return span_.get(); }

    /// Get the context for parent-child relationships.
    [[nodiscard]] opentelemetry::context::Context getContext() const { return context_; }

    friend class Tracer;

private:
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span_;
    opentelemetry::context::Context context_;
    bool statusSet_;
};

/// Global tracer instance (set up in main, used everywhere).
extern std::shared_ptr<Tracer> tracer;

}  // namespace creatures
