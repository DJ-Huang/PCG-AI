#include "runtime_probe.hpp"
#include <quickjs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kDiagnosticBytes = 4096;
constexpr const char* kFilename = "pcg-custom-function-probe.mjs";
constexpr const char* kSmoke = "export function main(ctx) { return ctx.answer; }";

enum class Code { Ok, SourceLimit, HostFailure, Exception, Interrupted, Cancelled,
                  ImportDenied, AsyncUnsupported, BadEntry, BadResult };
struct Result {
    Code code = Code::Ok;
    std::string phase, message, stack;
    double number = 0;
};
struct Limits {
    std::size_t memory = 16 * 1024 * 1024;
    std::size_t stack = 256 * 1024;
    std::size_t source = 256 * 1024;
    std::chrono::milliseconds timeout{3000};
    const std::atomic_bool* cancel = nullptr;
    std::atomic_bool* interruptObserved = nullptr;
    int* finalizations = nullptr; // Test instrumentation, owned by the caller.
};
struct State {
    Limits limits;
    Clock::time_point deadline;
    bool interrupted = false, cancelled = false, importDenied = false;
    std::size_t unhandled = 0;
};

class Value {
public:
    Value(JSContext* ctx, JSValue value) : ctx_(ctx), value_(value) {}
    ~Value() { JS_FreeValue(ctx_, value_); }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    JSValue get() const { return value_; }
private:
    JSContext* ctx_;
    JSValue value_;
};

int Interrupt(JSRuntime*, void* opaque) {
    auto& state = *static_cast<State*>(opaque);
    if (state.limits.interruptObserved)
        state.limits.interruptObserved->store(true, std::memory_order_release);
    if (state.limits.cancel && state.limits.cancel->load(std::memory_order_relaxed))
        state.cancelled = true;
    if (Clock::now() >= state.deadline) state.interrupted = true;
    return state.cancelled || state.interrupted; // Sticky even if user code catches an error.
}
char* DenyNormalize(JSContext* ctx, const char*, const char*, void* opaque) {
    static_cast<State*>(opaque)->importDenied = true;
    JS_ThrowReferenceError(ctx, "Module imports are disabled in the runtime spike");
    return nullptr;
}
JSModuleDef* DenyLoad(JSContext* ctx, const char*, void* opaque) {
    static_cast<State*>(opaque)->importDenied = true;
    JS_ThrowReferenceError(ctx, "Module imports are disabled in the runtime spike");
    return nullptr;
}
void TrackRejection(JSContext*, JSValueConst, JSValueConst, bool handled, void* opaque) {
    auto& state = *static_cast<State*>(opaque);
    if (!handled) ++state.unhandled;
    else if (state.unhandled) --state.unhandled;
}
void Finalize(JSRuntime*, void* opaque) { ++*static_cast<int*>(opaque); }

class Runtime {
public:
    explicit Runtime(const Limits& limits) : state{limits, Clock::now() + limits.timeout},
        rt(JS_NewRuntime(), JS_FreeRuntime), ctx(nullptr, JS_FreeContext) {
        if (!rt) throw std::runtime_error("JS_NewRuntime failed");
        JS_SetMemoryLimit(rt.get(), limits.memory);
        JS_SetMaxStackSize(rt.get(), limits.stack);
        JS_SetCanBlock(rt.get(), false);
        JS_SetInterruptHandler(rt.get(), Interrupt, &state);
        JS_SetModuleLoaderFunc(rt.get(), DenyNormalize, DenyLoad, &state);
        JS_SetHostPromiseRejectionTracker(rt.get(), TrackRejection, &state);
        // Probe-only leak assertions; never expose this abort policy to the server.
        JS_SetDumpFlags(rt.get(), JS_ABORT_ON_LEAKS);
        if (limits.finalizations && JS_AddRuntimeFinalizer(rt.get(), Finalize, limits.finalizations) < 0)
            throw std::runtime_error("Runtime finalizer registration failed");
        ctx.reset(JS_NewContext(rt.get()));
        if (!ctx) throw std::runtime_error("JS_NewContext failed (possibly memory budget)");
    }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    // Destruction is reverse declaration order: context, runtime, callback state.
    State state;
    std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> rt;
    std::unique_ptr<JSContext, decltype(&JS_FreeContext)> ctx;
};

void ClearException(JSContext* ctx) { Value ignored(ctx, JS_GetException(ctx)); }
std::string Text(JSContext* ctx, JSValueConst value) {
    std::size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, value);
    if (!text) { ClearException(ctx); return "<diagnostic unavailable>"; }
    // Keep the JS allocation owned even if constructing std::string throws.
    struct FreeText {
        JSContext* ctx;
        void operator()(const char* p) const { JS_FreeCString(ctx, p); }
    };
    std::unique_ptr<const char, FreeText> owned(text, FreeText{ctx});
    return std::string(text, std::min(length, kDiagnosticBytes));
}
Result Failure(Runtime& runtime, Code code, const char* phase, std::string message) {
    const auto& state = runtime.state;
    if (state.cancelled) { code = Code::Cancelled; message = "Script cancelled"; }
    else if (state.interrupted) { code = Code::Interrupted; message = "Script deadline exceeded"; }
    else if (state.importDenied) { code = Code::ImportDenied; message = "Module imports are disabled"; }
    return {code, phase, std::move(message), {}, 0};
}
Result Translate(Runtime& runtime, const char* phase, JSValue exception) {
    JSContext* ctx = runtime.ctx.get();
    Value owned(ctx, exception);
    std::string message = Text(ctx, exception);
    std::string stack;
    if (JS_IsObject(exception) && !runtime.state.cancelled && !runtime.state.interrupted) {
        Value property(ctx, JS_GetPropertyStr(ctx, exception, "stack"));
        if (JS_IsException(property.get())) ClearException(ctx);
        else if (JS_IsString(property.get())) stack = Text(ctx, property.get());
    }
    // toString()/stack getters are user code too; never disable the interrupt
    // hook to format them, and never lose a cancellation during formatting.
    Result result = Failure(runtime, Code::Exception, phase, std::move(message));
    result.stack = std::move(stack);
    return result;
}
Result PendingException(Runtime& runtime, const char* phase) {
    return Translate(runtime, phase, JS_GetException(runtime.ctx.get()));
}
bool HasAsyncWork(Runtime& runtime) {
    return JS_IsJobPending(runtime.rt.get()) || runtime.state.unhandled != 0;
}

// Intentionally test-only: numeric main(ctx) results exercise the embedding API.
// This is NOT the public Custom Function contract or a graph execution path.
Result Evaluate(const std::string& source, const Limits& limits = {}) {
    if (source.size() > limits.source)
        return {Code::SourceLimit, "source", "Source byte limit exceeded", {}, 0};
    if (!limits.memory || !limits.stack || limits.timeout.count() <= 0)
        return {Code::HostFailure, "limits", "Limits must be positive", {}, 0};
    if (limits.cancel && limits.cancel->load(std::memory_order_relaxed))
        return {Code::Cancelled, "before-start", "Script cancelled", {}, 0};
    try {
        Runtime runtime(limits);
        JSContext* ctx = runtime.ctx.get();
        Value compiled(ctx, JS_Eval(ctx, source.c_str(), source.size(), kFilename,
            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
        if (JS_IsException(compiled.get())) return PendingException(runtime, "compile");
        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled.get()));
        // JS_EvalFunction consumes its argument. Keep compiled alive until all
        // namespace/function values are released; no JSValue escapes this Cook.
        Value evaluated(ctx, JS_EvalFunction(ctx, JS_DupValue(ctx, compiled.get())));
        if (JS_IsException(evaluated.get())) return PendingException(runtime, "module");
        const auto state = JS_PromiseState(ctx, evaluated.get());
        if (state == JS_PROMISE_REJECTED)
            return Translate(runtime, "module", JS_PromiseResult(ctx, evaluated.get()));
        if (state == JS_PROMISE_PENDING || HasAsyncWork(runtime))
            return Failure(runtime, Code::AsyncUnsupported, "module", "Asynchronous module work is unsupported");
        Value ns(ctx, JS_GetModuleNamespace(ctx, module));
        if (JS_IsException(ns.get())) return PendingException(runtime, "namespace");
        Value main(ctx, JS_GetPropertyStr(ctx, ns.get(), "main"));
        if (JS_IsException(main.get())) return PendingException(runtime, "entry");
        if (!JS_IsFunction(ctx, main.get()))
            return Failure(runtime, Code::BadEntry, "entry", "Export a synchronous main(ctx) function");
        Value argument(ctx, JS_NewObject(ctx));
        if (JS_IsException(argument.get()) ||
            JS_SetPropertyStr(ctx, argument.get(), "answer", JS_NewInt32(ctx, 42)) < 0)
            return PendingException(runtime, "context");
        JSValue argv[] = {argument.get()}; // Borrowed by JS_Call.
        Value output(ctx, JS_Call(ctx, main.get(), JS_UNDEFINED, 1, argv));
        if (JS_IsException(output.get())) return PendingException(runtime, "call");
        if (JS_IsPromise(output.get()) || HasAsyncWork(runtime))
            return Failure(runtime, Code::AsyncUnsupported, "call", "Promises and queued jobs are unsupported");
        if (runtime.state.importDenied)
            return Failure(runtime, Code::ImportDenied, "call", "Module imports are disabled");
        double number = 0;
        if (!JS_IsNumber(output.get()) || JS_ToFloat64(ctx, &number, output.get()) < 0 || !std::isfinite(number))
            return Failure(runtime, Code::BadResult, "result", "The probe requires a finite numeric result");
        if (Interrupt(runtime.rt.get(), &runtime.state))
            return Failure(runtime, Code::Interrupted, "result", "Interrupted before publication");
        return {Code::Ok, "result", {}, {}, number};
    } catch (const std::exception& error) {
        return {Code::HostFailure, "host", error.what(), {}, 0};
    }
}

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void Expect42(const std::string& source = kSmoke, const Limits& limits = {}) {
    const auto result = Evaluate(source, limits);
    Require(result.code == Code::Ok && result.number == 42,
        "Expected 42; phase=" + result.phase + "; message=" + result.message);
}
void Probe(const std::string& name) {
    if (name == "smoke") {
        Expect42();
        Expect42("export function main() { let n=0; for(let i=0;i<7;i++) n+=6; return n; }");
        Require(Evaluate("export const main = 42;").code == Code::BadEntry, "Non-function main accepted");
        Require(Evaluate("export function main(){return {};}").code == Code::BadResult, "Object result accepted");
    } else if (name == "imports") {
        for (const char* specifier : {"std", "os", "./local.js", "node:fs", "https://example.invalid/module.js"}) {
            const auto result = Evaluate(std::string("import '") + specifier + "'; export function main(){return 42;}");
            Require(result.code == Code::ImportDenied, "Static import was not rejected by the loader policy");
        }
    } else if (name == "async") {
        for (const char* source : {
            "export async function main(){return 42;}",
            "await Promise.resolve(); export function main(){return 42;}",
            "export function main(){Promise.resolve().then(()=>{}); return 42;}",
            "export function main(){Promise.reject('unhandled'); return 42;}",
            "export function main(){return import('std');}",
            "export function main(){import('./local.js'); return 42;}"}) {
            const auto result = Evaluate(source);
            Require(result.code == Code::AsyncUnsupported || result.code == Code::ImportDenied,
                "Async work escaped synchronous evaluation: " + result.message);
        }
    } else if (name == "capabilities") {
        Expect42(R"JS(export function main() {
            const absent = ['std','os','require','process','fetch','setTimeout','Worker','window','document'];
            return absent.every(name => typeof globalThis[name] === 'undefined') ? 42 : 0;
        })JS");
        Expect42(R"JS(export function main() {
            if (typeof Atomics === 'undefined' || typeof SharedArrayBuffer === 'undefined') return 42;
            try { Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0); }
            catch (_) { return 42; }
            return 0;
        })JS");
    } else if (name == "exceptions") {
        const auto syntax = Evaluate("export function main( {");
        Require(syntax.code == Code::Exception && syntax.phase == "compile", "Syntax diagnostic missing");
        const auto error = Evaluate("export function main(){throw new Error('probe-sentinel');}");
        Require(error.code == Code::Exception && error.phase == "call", "Runtime diagnostic missing");
        Require(error.message.find("probe-sentinel") != std::string::npos, "Exception message missing");
        Require(error.stack.find(kFilename) != std::string::npos, "Source filename missing from stack");
        Require(Evaluate("export function main(){throw 17;}").message == "17", "Primitive throw lost");
        Expect42();
    } else if (name == "interrupt") {
        Limits limits; limits.timeout = std::chrono::milliseconds(100);
        for (const char* source : {
            "export function main(){while(true){}}",
            "while(true){}; export function main(){return 42;}",
            "export function main(){for(;;){try{for(;;){}}catch(_){}}}"})
            Require(Evaluate(source, limits).code == Code::Interrupted, "Infinite loop was not interrupted");
        Expect42();
    } else if (name == "cancellation") {
        std::atomic_bool cancel{true}, observed{false};
        Limits limits; limits.cancel = &cancel;
        Require(Evaluate(kSmoke, limits).code == Code::Cancelled, "Pre-cancellation ignored");
        cancel.store(false);
        limits.interruptObserved = &observed;
        Result result;
        std::thread worker([&] { result = Evaluate("export function main(){while(true){}}", limits); });
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (!observed.load(std::memory_order_acquire) && Clock::now() < deadline)
            std::this_thread::yield();
        const bool started = observed.load(std::memory_order_acquire);
        cancel.store(true, std::memory_order_relaxed);
        worker.join();
        Require(started && result.code == Code::Cancelled, "In-flight cancellation failed");
    } else if (name == "memory") {
        Limits limits; limits.memory = 4 * 1024 * 1024;
        for (const char* source : {
            "export function main(){return new ArrayBuffer(64*1024*1024).byteLength;}",
            "export function main(){return 'x'.repeat(64*1024*1024).length;}"}) {
            const auto result = Evaluate(source, limits);
            Require(result.code == Code::Exception && result.phase == "call", "Memory allocation was not bounded");
        }
        limits.memory = 1;
        Require(Evaluate(kSmoke, limits).code == Code::HostFailure, "Context creation ignored tiny budget");
        Expect42();
    } else if (name == "stack") {
        const auto result = Evaluate("export function main(){function f(){return 1+f();}return f();}");
        Require(result.code == Code::Exception && result.message.find("stack") != std::string::npos,
            "Recursion did not produce a stack diagnostic");
    } else if (name == "teardown") {
        int finalizations = 0;
        Limits limits; limits.finalizations = &finalizations;
        for (int i = 0; i < 200; ++i) {
            if (i % 2 == 0) Expect42("export function main(){if(globalThis.saved) return 0; globalThis.saved=1; return 42;}", limits);
            else Require(Evaluate("export function main(){throw new Error('teardown');}", limits).code == Code::Exception,
                "Expected teardown error");
            Require(finalizations == i + 1, "Runtime finalizer did not run exactly once");
        }
    } else if (name == "concurrency") {
        std::atomic_int failures{0};
        std::vector<std::thread> workers;
        // Each thread creates, uses, and destroys its OWN runtime. No shared Core Cook.
        try {
            for (int t = 0; t < 4; ++t) workers.emplace_back([&] {
                try {
                    for (int i = 0; i < 50; ++i)
                        Expect42("export function main(){globalThis.n=(globalThis.n||0)+1; return globalThis.n===1?42:0;}");
                } catch (...) { ++failures; }
            });
        } catch (...) {
            for (auto& worker : workers) worker.join();
            throw;
        }
        for (auto& worker : workers) worker.join();
        Require(failures.load() == 0, "Independent runtimes interfered");
    } else if (name == "source_limit") {
        Limits limits; limits.source = 8;
        Require(Evaluate(kSmoke, limits).code == Code::SourceLimit, "Source byte limit ignored");
        limits.source = 256 * 1024; limits.memory = 0;
        Require(Evaluate(kSmoke, limits).code == Code::HostFailure, "Unlimited memory accepted");
    } else if (name == "diagnostics") {
        const auto large = Evaluate("export function main(){throw new Error('x'.repeat(20000));}");
        Require(large.code == Code::Exception && !large.message.empty() &&
            large.message.size() <= kDiagnosticBytes && large.stack.size() <= kDiagnosticBytes,
            "Retained diagnostic is not bounded");
        Limits limits; limits.timeout = std::chrono::milliseconds(100);
        const auto hostile = Evaluate("export function main(){throw {toString(){while(true){}}};}", limits);
        Require(hostile.code == Code::Interrupted, "Exception formatting bypassed the deadline");
        Expect42();
    } else throw std::runtime_error("Unknown probe: " + name);
}

std::vector<double> Measure(unsigned samples, const std::function<void()>& operation) {
    for (unsigned i = 0; i < 20; ++i) operation();
    std::vector<double> times;
    times.reserve(samples);
    for (unsigned i = 0; i < samples; ++i) {
        const auto begin = Clock::now();
        operation();
        times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count());
    }
    std::sort(times.begin(), times.end());
    return times;
}
void PrintMetric(const char* name, const std::vector<double>& times, bool comma) {
    double sum = 0;
    for (double time : times) sum += time;
    const auto percentile = [&](double p) { return times[static_cast<std::size_t>(std::ceil(p * times.size())) - 1]; };
    std::cout << (comma ? "," : "") << '"' << name << "\":{\"mean_us\":" << sum / times.size()
        << ",\"p50_us\":" << percentile(0.50) << ",\"p95_us\":" << percentile(0.95)
        << ",\"min_us\":" << times.front() << ",\"max_us\":" << times.back() << '}';
}
} // namespace

extern "C" int pcg_scripting_run_probe(const char* name) {
    try {
        if (!name) return 2;
        Probe(name);
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (name ? name : "<null>") << ": " << error.what() << '\n';
        return 1;
    } catch (...) { return 1; }
}
extern "C" int pcg_scripting_benchmark(unsigned samples) {
    if (samples < 5 || samples > 10000) return 2;
    try {
        const auto lifecycle = Measure(samples, [] { Runtime runtime(Limits{}); });
        const auto fresh = Measure(samples, [] { Expect42(); });
        const auto loop = Measure(samples, [] {
            const auto result = Evaluate("export function main(){let n=0;for(let i=0;i<10000;i++)n+=i;return n;}");
            Require(result.code == Code::Ok && result.number == 49995000, "Benchmark result mismatch");
        });
        std::cout << std::setprecision(9) << "{\"engine\":\"QuickJS-NG\",\"version\":\"" PCG_QUICKJS_VERSION
            "\",\"commit\":\"" PCG_QUICKJS_COMMIT "\",\"compiler\":\"" PCG_PROBE_COMPILER
            "\",\"pointer_bits\":" << sizeof(void*) * 8 << ",\"samples\":" << samples
            << ",\"warmup_samples\":20,\"clock\":\"steady_clock\",\"metrics\":{";
        PrintMetric("runtime_context_create_destroy", lifecycle, false);
        PrintMetric("fresh_module_compile_call_teardown", fresh, true);
        PrintMetric("fresh_module_10000_iterations_total", loop, true);
        std::cout << "}}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    } catch (...) { return 1; }
}
