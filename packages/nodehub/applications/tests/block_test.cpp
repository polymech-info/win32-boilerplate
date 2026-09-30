#include "commons-test.h"
#include "blocks/block.h"
#include "types.h" // Include for the application's Node struct

/// A mock block class that inherits from ParameterizedBlock, just like real blocks.
class MockBlock : public ParameterizedBlock
{
public:
    MockBlock(int id) : ParameterizedBlock(id, "MockBlock") {}

    // Implement the remaining pure virtual functions.
    // 'Render' is already implemented by ParameterizedBlock.
    void Build(Node& node, App* app) override {}
    NH_CSTRING GetBlockType() const override { return "MockBlockType"; }
};

// Test fixture for BlockRegistry tests.
// Inherits from AppTest to get a valid App instance, though this specific test doesn't use it.
class BlockRegistryTest : public AppTest
{};

TEST_F(BlockRegistryTest, RegistrationAndCreation)
{
    auto& registry = BlockRegistry::Instance();
    const char* mockBlockTypeName = "MyMockBlock";

    // Register our mock block
    registry.RegisterBlock(mockBlockTypeName, [](int id) -> Block* {
        return new MockBlock(id);
    });

    // Attempt to create the registered block
    Block* createdBlock = registry.CreateBlock(mockBlockTypeName, 42);
    ASSERT_NE(createdBlock, nullptr) << "Block creation failed for a registered type.";
    EXPECT_EQ(createdBlock->GetID(), 42);
    EXPECT_STREQ(createdBlock->GetBlockType(), "MockBlockType");
    delete createdBlock;

    // Attempt to create a non-existent block
    Block* nonExistentBlock = registry.CreateBlock("NonExistentBlock", 100);
    EXPECT_EQ(nonExistentBlock, nullptr) << "Block creation should fail for an unregistered type.";
}
