#include "commons-test.h"
#include "core/ParameterIn.h"
#include "core/ParameterOut.h"
#include "core/Context.h"
#include "enums.h"

// Test fixture for Parameter tests
class ParameterTest : public AppTest {};

TEST_F(ParameterTest, CreateParameters) {
    ASSERT_NE(m_App, nullptr);
    auto* context = m_App->GetContext();
    ASSERT_NE(context, nullptr);

    // Create an output parameter
    auto* p_out = context->CreateParameterOut("MyFloatOut", NHP_GUID_FLOAT);
    ASSERT_NE(p_out, nullptr);
    EXPECT_STREQ(p_out->GetName(), "MyFloatOut");
    EXPECT_EQ(p_out->GetClassID(), NHCID_PARAMETEROUT);

    // Create an input parameter
    auto* p_in = context->CreateParameterIn("MyFloatIn", NHP_GUID_FLOAT);
    ASSERT_NE(p_in, nullptr);
    EXPECT_STREQ(p_in->GetName(), "MyFloatIn");
    EXPECT_EQ(p_in->GetClassID(), NHCID_PARAMETERIN);

    delete p_out;
    delete p_in;
}

TEST_F(ParameterTest, LinkParametersAndGetValue) {
    ASSERT_NE(m_App, nullptr);
    auto* context = m_App->GetContext();
    ASSERT_NE(context, nullptr);

    // 1. Create parameters
    auto* p_out = context->CreateParameterOut("SourceFloat", NHP_GUID_FLOAT);
    auto* p_in = context->CreateParameterIn("DestFloat", NHP_GUID_FLOAT);

    // 2. Set a value on the output parameter
    float out_value = 3.14f;
    p_out->SetValue(&out_value, sizeof(float));

    // 3. Link the input parameter to the output parameter
    p_in->SetDirectSource(p_out);

    // 4. Get the value from the input parameter
    float in_value = 0.0f;
    p_in->GetValue(&in_value);

    // 5. Verify the value is correct
    EXPECT_FLOAT_EQ(in_value, out_value);

    // 6. Test GetReadDataPtr
    void* data_ptr = p_in->GetReadDataPtr();
    ASSERT_NE(data_ptr, nullptr);
    EXPECT_FLOAT_EQ(*(static_cast<float*>(data_ptr)), out_value);

    delete p_out;
    delete p_in;
}

TEST_F(ParameterTest, OutputToMultipleInputs) {
    ASSERT_NE(m_App, nullptr);
    auto* context = m_App->GetContext();
    ASSERT_NE(context, nullptr);

    // 1. Create one output and multiple input parameters
    auto* p_out = context->CreateParameterOut("SourceFloat", NHP_GUID_FLOAT);
    auto* p_in1 = context->CreateParameterIn("DestFloat1", NHP_GUID_FLOAT);
    auto* p_in2 = context->CreateParameterIn("DestFloat2", NHP_GUID_FLOAT);
    auto* p_in3 = context->CreateParameterIn("DestFloat3", NHP_GUID_FLOAT);

    // 2. Set a value on the output parameter
    float out_value = 42.0f;
    p_out->SetValue(&out_value, sizeof(float));

    // 3. Link all input parameters to the single output parameter
    p_in1->SetDirectSource(p_out);
    p_in2->SetDirectSource(p_out);
    p_in3->SetDirectSource(p_out);

    // 4. Verify the value for all inputs
    float in_value1 = 0.0f, in_value2 = 0.0f, in_value3 = 0.0f;
    p_in1->GetValue(&in_value1);
    p_in2->GetValue(&in_value2);
    p_in3->GetValue(&in_value3);

    EXPECT_FLOAT_EQ(in_value1, out_value);
    EXPECT_FLOAT_EQ(in_value2, out_value);
    EXPECT_FLOAT_EQ(in_value3, out_value);

    delete p_out;
    delete p_in1;
    delete p_in2;
    delete p_in3;
}

TEST_F(ParameterTest, SingleSourceForInput) {
    ASSERT_NE(m_App, nullptr);
    auto* context = m_App->GetContext();
    ASSERT_NE(context, nullptr);

    // 1. Create multiple source parameters and inputs
    auto* p_out1 = context->CreateParameterOut("Source1", NHP_GUID_INT);
    auto* p_out2 = context->CreateParameterOut("Source2", NHP_GUID_INT);
    auto* p_in1 = context->CreateParameterIn("Input1", NHP_GUID_INT);
    auto* p_in2 = context->CreateParameterIn("Input2", NHP_GUID_INT);

    int val1 = 111, val2 = 222;
    p_out1->SetValue(&val1, sizeof(int));
    p_out2->SetValue(&val2, sizeof(int));

    // 2. Link p_in2 to p_out2
    p_in2->SetDirectSource(p_out2);
    EXPECT_EQ(p_in2->GetRealSource(), p_out2);

    // 3. Link p_in1 to p_out1 initially
    p_in1->SetDirectSource(p_out1);
    EXPECT_EQ(p_in1->GetRealSource(), p_out1);
    EXPECT_EQ(p_in1->GetSharedSource(), nullptr);

    // 4. Now, share p_in1 with p_in2. This should override the direct source.
    p_in1->ShareSourceWith(p_in2);
    EXPECT_EQ(p_in1->GetDirectSource(), nullptr);
    EXPECT_EQ(p_in1->GetSharedSource(), p_in2);
    
    // 5. The real source of p_in1 should now be p_out2 (through p_in2)
    EXPECT_EQ(p_in1->GetRealSource(), p_out2);

    // 6. Verify the value comes from the new real source
    int in_value = 0;
    p_in1->GetValue(&in_value);
    EXPECT_EQ(in_value, val2);

    delete p_out1;
    delete p_out2;
    delete p_in1;
    delete p_in2;
}
