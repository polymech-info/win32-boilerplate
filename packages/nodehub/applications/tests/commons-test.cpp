#include "commons-test.h"
#include "base.h" // For g_Application

void AppTest::SetUp()
{
    // Create an App instance with an empty argument map.
    // The Application constructor will set g_Application = this.
    // This mimics the setup in entry_point.cpp without creating a window.
    m_App = std::make_unique<App>("TestApp", ArgsMap{});
}

void AppTest::TearDown()
{
    m_App.reset();
    g_Application = nullptr;
}
