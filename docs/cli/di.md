# CLI Command Registration with DI and Runtime Introspection (C++20)

## Problem Statement

The current `pm_image_register_cli.cpp` uses a `CmdFlags` bitmask + preprocessor conditionals to enable commands at compile time. This approach:

- Requires manual synchronization between CMake feature flags and C++ code
- Provides no runtime introspection ("which commands are enabled?")
- Lacks dependency injection (hard to test, hard to mock)
- Scales poorly as feature count grows

## Goals

1. **Compile-time feature pruning** — disabled commands compile to zero code
2. **Runtime introspection** — query enabled commands, descriptions, dependencies
3. **Dependency injection** — commands receive dependencies via constructor/interface
4. **Type safety** — compiler validates command implementations
5. **Zero overhead** — enabled commands have no runtime cost vs. current approach

## Design Overview

### 1. Command Feature Concept

Every command is a `struct` satisfying the `CommandFeature` concept:

```cpp
#include <concepts>
#include <string_view>

template<typename F>
concept CommandFeature = requires(F f) {
    // Metadata for introspection
    { F::name() } -> std::convertible_to<std::string_view>;
    { F::description() } -> std::convertible_to<std::string_view>;
    { F::enabled() } -> std::same_as<bool>;
    
    // Registration behavior
    { F::register_cmd(app, state, deps) } -> std::same_as<void>;
    
    // Dependency type (injected at registration time)
    typename F::dependencies;
};
```

### 2. Feature Implementation Pattern

Each command lives in its own header with self-contained metadata:

```cpp
// src/cli/features/pm_image_cmd_serve_feature.hpp
#pragma once
#include "cli/command_feature.hpp"
#include "http/http_server_factory.hpp"  // DI interface

namespace pm::cli {

struct ServeFeature {
    // ── Compile-time enable (from CMake) ─────────────────────────────────────
    static constexpr bool enabled() {
#if defined(FEATURE_SERVE) && FEATURE_SERVE
        return true;
#else
        return false;
#endif
    }

    // ── Metadata ────────────────────────────────────────────────────────────
    static constexpr std::string_view name() { return "serve"; }
    static constexpr std::string_view description() { 
        return "HTTP REST API server (OpenAPI spec)"; 
    }

    // ── Dependencies (constructor-injected) ────────────────────────────────
    struct dependencies {
        http::IHttpServerFactory* server_factory{nullptr};
        // Add more: metrics, config, logger, etc.
    };

    // ── Registration (called only if enabled() == true) ─────────────────────
    static void register_cmd(CLI::App& app, 
                           PmImageCliState& state,
                           const dependencies& deps) {
        auto* cmd = app.add_subcommand(
            std::string(name()), 
            std::string(description())
        );
        
        cmd->add_option("-p,--port", state.serve_port, "Port to listen on")
            ->default_val(8080);
        
        cmd->callback([&state, &deps]() {
            if (!deps.server_factory) {
                throw std::runtime_error("Serve: server_factory not injected");
            }
            auto server = deps.server_factory->create(state.serve_port);
            server->run();
        });
    }
};

} // namespace pm::cli
```

### 3. Active Features Pack

Central registry as a `std::tuple` of feature types. This is the single point where CMake flags determine which commands are available:

```cpp
// src/cli/active_features.hpp
#pragma once
#include "features/pm_image_cmd_serve_feature.hpp"
#include "features/pm_image_cmd_ipc_feature.hpp"
// ... other features

namespace pm::cli {

// Tuple of all potentially-available command features
// CMake-controlled: only features with FEATURE_X=ON are included
using active_features = std::tuple<
#if defined(FEATURE_SERVE) && FEATURE_SERVE
    ServeFeature,
#endif
#if defined(FEATURE_IPC) && FEATURE_IPC
    IpcFeature,
#endif
#if defined(FEATURE_STT) && FEATURE_STT
    AudioFeature,
#endif
    // Always available
    ResizeFeature,
    CompressFeature,
    MetaFeature
>;

} // namespace pm::cli
```

### 4. Runtime Introspection

Extract metadata from the feature pack at compile time:

```cpp
// src/cli/introspection.hpp
#pragma once
#include <array>
#include <string_view>
#include <vector>

namespace pm::cli {

struct CommandInfo {
    std::string_view name;
    std::string_view description;
    bool enabled;
};

// Build metadata array at compile time
template<typename... Fs>
consteval std::array<CommandInfo, sizeof...(Fs)> build_metadata() {
    return { CommandInfo{Fs::name(), Fs::description(), Fs::enabled()}... };
}

// Global metadata for active features
inline constexpr auto k_command_metadata = 
    std::apply([](auto... f) { 
        return build_metadata<decltype(f)...>(); 
    }, active_features{});

// Runtime queries (for help, MCP discovery, UI)
inline std::vector<CommandInfo> get_enabled_commands() {
    std::vector<CommandInfo> result;
    for (const auto& cmd : k_command_metadata) {
        if (cmd.enabled) result.push_back(cmd);
    }
    return result;
}

inline bool is_command_enabled(std::string_view name) {
    for (const auto& cmd : k_command_metadata) {
        if (cmd.name == name) return cmd.enabled;
    }
    return false;
}

} // namespace pm::cli
```

### 5. Simple DI Container

Minimal type-erased container for dependency resolution:

```cpp
// src/cli/di_container.hpp
#pragma once
#include <memory>
#include <functional>
#include <unordered_map>
#include <typeindex>

namespace pm::cli {

class DIContainer {
    std::unordered_map<std::type_index, std::any> services_;

public:
    // Register singleton
    template<typename Interface, typename Impl>
    void bind_singleton(std::shared_ptr<Impl> instance) {
        static_assert(std::is_base_of_v<Interface, Impl>);
        services_[std::type_index(typeid(Interface))] = std::move(instance);
    }

    // Resolve dependency
    template<typename Interface>
    Interface* get() const {
        auto it = services_.find(std::type_index(typeid(Interface)));
        if (it != services_.end()) {
            return std::any_cast<std::shared_ptr<Interface>>(it->second).get();
        }
        return nullptr;
    }
};

} // namespace pm::cli
```

### 6. Main Registration Function

Templated registration that expands the feature pack:

```cpp
// src/cli/pm_image_register_cli.hpp
#pragma once
#include "active_features.hpp"
#include "introspection.hpp"
#include "di_container.hpp"

namespace pm::cli {

// Helper: register single feature if enabled
template<typename Feature>
void register_feature(CLI::App& app, 
                      PmImageCliState& state, 
                      const DIContainer& di) {
    if constexpr (Feature::enabled()) {
        // Build dependencies from DI container
        typename Feature::dependencies deps{
            .server_factory = di.get<http::IHttpServerFactory>(),
            // ... map other deps
        };
        Feature::register_cmd(app, state, deps);
    }
    // If !enabled(), this entire function body is compiled away
}

// Main entry point
inline void pm_image_register_cli(CLI::App& app, 
                                   PmImageCliState& state,
                                   const DIContainer& di) {
    // Register global options (same as current)
    register_global_options(app, state);
    
    // Expand feature pack
    std::apply([&](auto... feature) {
        (register_feature<decltype(feature)>(app, state, di), ...);
    }, active_features{});
}

} // namespace pm::cli
```

## Migration Strategy

### Phase 1: Add Infrastructure (1 PR)
- Create `command_feature.hpp` with concept definition
- Create `di_container.hpp`
- Create `active_features.hpp` (empty tuple initially)
- Create `introspection.hpp`

### Phase 2: Convert One Command (1 PR per command)
Pick a simple command (e.g., `meta`) to validate the pattern:
- Move from `pm_image_cmd_meta.hpp` to `features/meta_feature.hpp`
- Add to `active_features.hpp`
- Update `pm_image_register_cli.cpp` to use new path (keep old as fallback)

### Phase 3: Migrate Remaining Commands
- Convert each command to feature struct
- Add to `active_features.hpp` tuple

### Phase 4: Cleanup (1 PR)
- Remove old `CmdFlags` enum
- Remove `pm_image_register_cli.cpp` (replace with header-only)
- Remove conditional `#include` blocks

## Benefits

| Metric | Before | After |
|--------|--------|-------|
| **Compile-time pruning** | `#if` around code blocks | `if constexpr` (zero overhead) |
| **Introspection** | None | `k_command_metadata` array |
| **DI** | Global state / hardcoded | Constructor injection |
| **Testability** | Hard (global deps) | Easy (mock deps injected) |
| **Type safety** | Bitmask flags | Concepts enforce interface |
| **Build complexity** | Many `#ifdef` branches | Single tuple definition |

## Example: Testing with DI

```cpp
// Mock HTTP server for testing
struct MockHttpServerFactory : http::IHttpServerFactory {
    int create_calls = 0;
    int last_port = 0;
    
    std::unique_ptr<HttpServer> create(int port) override {
        ++create_calls;
        last_port = port;
        return std::make_unique<MockHttpServer>();
    }
};

TEST(ServeCommand, RegistersCorrectly) {
    CLI::App app;
    PmImageCliState state;
    pm::cli::DIContainer di;
    
    auto mock = std::make_shared<MockHttpServerFactory>();
    di.bind_singleton<http::IHttpServerFactory>(mock);
    
    pm::cli::ServeFeature::register_cmd(app, state, {
        .server_factory = mock.get()
    });
    
    // Parse and execute
    app.parse({"serve", "--port", "9999"});
    
    EXPECT_EQ(mock->create_calls, 1);
    EXPECT_EQ(mock->last_port, 9999);
}
```

## ARM and Low-Spec Considerations

### Supported ARM Targets

| Target | Status | Compiler Requirements |
|--------|--------|----------------------|
| **macOS Apple Silicon** (ARM64) | Fully supported | Xcode 13+ (Clang 13+) |
| **Windows on ARM** (ARM64) | Fully supported | MSVC 2019+ |
| **Linux ARM64** | Fully supported | GCC 10+, Clang 12+ |

All C++20 features used (concepts, `consteval`, fold expressions) are fully supported on these targets.

### Binary Size Trade-offs

The template-based design generates code per command at compile time. For **desktop targets** (pm-image's actual use case), this is negligible overhead. For **constrained ARM devices**, consider:

| Approach | Code Size | Runtime Cost | Best For |
|----------|-----------|--------------|----------|
| **Template pack** (this design) | Slightly larger (monomorphization) | Zero (inlined) | Desktop, servers |
| **Virtual interface** | Smaller (single vtable) | Virtual call | Embedded, constrained |

If binary size becomes critical on ARM, switch to virtual interfaces:

```cpp
// Smaller binary alternative for constrained targets
struct ICommandFeature {
    virtual std::string_view name() const = 0;
    virtual bool enabled() const = 0;
    virtual void register_cmd(CLI::App&, PmImageCliState&, const DIContainer&) = 0;
};

// Static registration table instead of template pack
static const std::array<ICommandFeature*, 10> s_commands = {
    &s_serve_feature, &s_ipc_feature, // ...
};
```

**Note**: pm-image targets desktop with libvips/WebView2 — constrained ARM (Cortex-M, <1MB RAM) is not a realistic deployment target.

## Open Questions

1. **Dependency lifecycle**: Singleton vs. per-request vs. scoped?
2. **Cross-feature dependencies**: Should Feature A declare it needs Feature B?
3. **Dynamic reloading**: Do we ever want to enable commands at runtime?
4. **MCP exposure**: Should introspection data be exposed via MCP protocol?

## References

- `ref/main.cpp` — CRTP feature composition experiment
- `ref/main.c` — Arduino-style component networking pattern
- Current: `src/cli/pm_image_register_cli.cpp` — bitmask approach
