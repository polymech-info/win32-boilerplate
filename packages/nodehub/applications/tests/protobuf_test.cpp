#include "commons-test.h"
#include "core/ParameterOut.h"
#include "enums.h"
#include "protos/test_primitives.pb.h" // Generated protobuf header

// Test fixture for Protobuf tests
class ProtobufTest : public AppTest {};

TEST_F(ProtobufTest, IntSerialization) {
    auto* context = m_App->GetContext();
    auto* p_out = context->CreateParameterOut("MyInt", NHP_GUID_INT);

    int out_value = 12345;
    p_out->SetValue(&out_value);

    // Serialize
    nodehub::primitives::IntValue proto_msg;
    proto_msg.set_value(*(int*)p_out->GetReadDataPtr());
    std::string serialized_data;
    ASSERT_TRUE(proto_msg.SerializeToString(&serialized_data));

    // Deserialize
    nodehub::primitives::IntValue new_proto_msg;
    ASSERT_TRUE(new_proto_msg.ParseFromString(serialized_data));

    // Verify
    EXPECT_EQ(new_proto_msg.value(), out_value);

    delete p_out;
}

TEST_F(ProtobufTest, FloatSerialization) {
    auto* context = m_App->GetContext();
    auto* p_out = context->CreateParameterOut("MyFloat", NHP_GUID_FLOAT);

    float out_value = 3.14f;
    p_out->SetValue(&out_value);

    // Serialize
    nodehub::primitives::FloatValue proto_msg;
    proto_msg.set_value(*(float*)p_out->GetReadDataPtr());
    std::string serialized_data;
    ASSERT_TRUE(proto_msg.SerializeToString(&serialized_data));

    // Deserialize
    nodehub::primitives::FloatValue new_proto_msg;
    ASSERT_TRUE(new_proto_msg.ParseFromString(serialized_data));

    // Verify
    EXPECT_FLOAT_EQ(new_proto_msg.value(), out_value);

    delete p_out;
}

TEST_F(ProtobufTest, BoolSerialization) {
    auto* context = m_App->GetContext();
    auto* p_out = context->CreateParameterOut("MyBool", NHP_GUID_BOOL);

    bool out_value = true;
    p_out->SetValue(&out_value);

    // Serialize
    nodehub::primitives::BoolValue proto_msg;
    proto_msg.set_value(*(bool*)p_out->GetReadDataPtr());
    std::string serialized_data;
    ASSERT_TRUE(proto_msg.SerializeToString(&serialized_data));

    // Deserialize
    nodehub::primitives::BoolValue new_proto_msg;
    ASSERT_TRUE(new_proto_msg.ParseFromString(serialized_data));

    // Verify
    EXPECT_EQ(new_proto_msg.value(), out_value);

    delete p_out;
}

TEST_F(ProtobufTest, StringSerialization) {
    auto* context = m_App->GetContext();
    auto* p_out = context->CreateParameterOut("MyString", NHP_GUID_STRING);

    const char* out_value = "Hello, Protobuf!";
    p_out->SetValue(out_value, strlen(out_value) + 1);

    // Serialize
    nodehub::primitives::StringValue proto_msg;
    proto_msg.set_value((char*)p_out->GetReadDataPtr());
    std::string serialized_data;
    ASSERT_TRUE(proto_msg.SerializeToString(&serialized_data));

    // Deserialize
    nodehub::primitives::StringValue new_proto_msg;
    ASSERT_TRUE(new_proto_msg.ParseFromString(serialized_data));

    // Verify
    EXPECT_STREQ(new_proto_msg.value().c_str(), out_value);

    delete p_out;
}

