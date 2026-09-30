#pragma once
#include "gtest/gtest.h"
#include "app.h"
#include "core/Context.h"

#include <memory>

// A base test fixture that provides a valid `App` instance for testing.
// This avoids creating windows or a renderer, making it suitable for unit/functional tests.
class AppTest : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    // Use a unique_ptr to manage the lifetime of the app instance.
    // It's a pointer to App, not Application, to allow access to NodeHub-specific functionality.
    std::unique_ptr<App> m_App;
};
