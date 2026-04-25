/**
 * @file ModelData_uTest.cpp
 * @brief Unit tests for ModelData typed container.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using system_core::data::DataCategory;
using system_core::data::Input;
using system_core::data::ModelData;
using system_core::data::Output;
using system_core::data::State;
using system_core::data::StaticParam;
using system_core::data::TunableParam;

/* ----------------------------- Test Structs ----------------------------- */

namespace {

struct SimpleData {
  std::uint32_t value;
};

struct PhysicalConstants {
  double gravity;
  double airDensity;
};

struct ControlGains {
  double kp;
  double ki;
  double kd;
};

struct SensorReading {
  std::uint64_t timestamp;
  double temperature;
  double pressure;
};

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test ModelData default constructs with zero-initialized data. */
TEST(ModelData, DefaultConstruction) {
  ModelData<SimpleData, DataCategory::STATE> data;
  EXPECT_EQ(data.get().value, 0u);
}

/** @test ModelData can be constructed with initial value. */
TEST(ModelData, ValueConstruction) {
  SimpleData initial{42};
  ModelData<SimpleData, DataCategory::STATE> data{initial};
  EXPECT_EQ(data.get().value, 42u);
}

/* ----------------------------- Read Access ----------------------------- */

/** @test Const get() returns data reference. */
TEST(ModelData, ConstGetReturnsReference) {
  PhysicalConstants constants{9.81, 1.225};
  const ModelData<PhysicalConstants, DataCategory::STATIC_PARAM> data{constants};

  EXPECT_DOUBLE_EQ(data.get().gravity, 9.81);
  EXPECT_DOUBLE_EQ(data.get().airDensity, 1.225);
}

/** @test Arrow operator provides const member access. */
TEST(ModelData, ArrowOperatorConstAccess) {
  ControlGains gains{1.0, 0.1, 0.01};
  const ModelData<ControlGains, DataCategory::TUNABLE_PARAM> data{gains};

  EXPECT_DOUBLE_EQ(data->kp, 1.0);
  EXPECT_DOUBLE_EQ(data->ki, 0.1);
  EXPECT_DOUBLE_EQ(data->kd, 0.01);
}

/** @test Dereference operator provides const access. */
TEST(ModelData, DereferenceOperatorConstAccess) {
  SimpleData initial{100};
  const ModelData<SimpleData, DataCategory::STATE> data{initial};

  EXPECT_EQ((*data).value, 100u);
}

/** @test Const ptr() returns pointer to data. */
TEST(ModelData, ConstPtrReturnsPointer) {
  SimpleData initial{50};
  const ModelData<SimpleData, DataCategory::INPUT> data{initial};

  const SimpleData* p = data.ptr();
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->value, 50u);
}

/* ----------------------------- Write Access ----------------------------- */

/** @test Mutable get() allows modification for non-STATIC categories. */
TEST(ModelData, MutableGetAllowsModification) {
  ModelData<ControlGains, DataCategory::TUNABLE_PARAM> data{{1.0, 0.1, 0.01}};

  data.get().kp = 2.0;
  EXPECT_DOUBLE_EQ(data->kp, 2.0);
}

/** @test Mutable arrow operator allows modification. */
TEST(ModelData, MutableArrowOperatorAllowsModification) {
  ModelData<SensorReading, DataCategory::INPUT> data{};

  data->timestamp = 1000;
  data->temperature = 25.0;
  data->pressure = 101325.0;

  EXPECT_EQ(data->timestamp, 1000u);
  EXPECT_DOUBLE_EQ(data->temperature, 25.0);
  EXPECT_DOUBLE_EQ(data->pressure, 101325.0);
}

/** @test set() replaces data value. */
TEST(ModelData, SetReplacesValue) {
  ModelData<SimpleData, DataCategory::STATE> data{{0}};

  SimpleData newValue{999};
  data.set(newValue);

  EXPECT_EQ(data.get().value, 999u);
}

/** @test Mutable ptr() returns modifiable pointer. */
TEST(ModelData, MutablePtrReturnsModifiablePointer) {
  ModelData<SimpleData, DataCategory::OUTPUT> data{{0}};

  SimpleData* p = data.ptr();
  p->value = 123;

  EXPECT_EQ(data.get().value, 123u);
}

/* ----------------------------- Category Queries ----------------------------- */

/** @test category() returns correct category. */
TEST(ModelData, CategoryReturnsCorrectValue) {
  ModelData<SimpleData, DataCategory::STATIC_PARAM> staticData{};
  ModelData<SimpleData, DataCategory::TUNABLE_PARAM> tunableData{};
  ModelData<SimpleData, DataCategory::STATE> stateData{};
  ModelData<SimpleData, DataCategory::INPUT> inputData{};
  ModelData<SimpleData, DataCategory::OUTPUT> outputData{};

  EXPECT_EQ(staticData.category(), DataCategory::STATIC_PARAM);
  EXPECT_EQ(tunableData.category(), DataCategory::TUNABLE_PARAM);
  EXPECT_EQ(stateData.category(), DataCategory::STATE);
  EXPECT_EQ(inputData.category(), DataCategory::INPUT);
  EXPECT_EQ(outputData.category(), DataCategory::OUTPUT);
}

/** @test isReadOnly() returns true only for STATIC_PARAM. */
TEST(ModelData, IsReadOnlyCorrect) {
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::STATIC_PARAM>::isReadOnly()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::TUNABLE_PARAM>::isReadOnly()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATE>::isReadOnly()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::INPUT>::isReadOnly()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::OUTPUT>::isReadOnly()));
}

/** @test isParam() returns true for parameter categories. */
TEST(ModelData, IsParamCorrect) {
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::STATIC_PARAM>::isParam()));
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::TUNABLE_PARAM>::isParam()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATE>::isParam()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::INPUT>::isParam()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::OUTPUT>::isParam()));
}

/** @test isModelInput() returns true for input-like categories. */
TEST(ModelData, IsModelInputCorrect) {
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::STATIC_PARAM>::isModelInput()));
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::TUNABLE_PARAM>::isModelInput()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATE>::isModelInput()));
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::INPUT>::isModelInput()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::OUTPUT>::isModelInput()));
}

/** @test isModelOutput() returns true only for OUTPUT. */
TEST(ModelData, IsModelOutputCorrect) {
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATIC_PARAM>::isModelOutput()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::TUNABLE_PARAM>::isModelOutput()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATE>::isModelOutput()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::INPUT>::isModelOutput()));
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::OUTPUT>::isModelOutput()));
}

/** @test isState() returns true only for STATE. */
TEST(ModelData, IsStateCorrect) {
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::STATIC_PARAM>::isState()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::TUNABLE_PARAM>::isState()));
  EXPECT_TRUE((ModelData<SimpleData, DataCategory::STATE>::isState()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::INPUT>::isState()));
  EXPECT_FALSE((ModelData<SimpleData, DataCategory::OUTPUT>::isState()));
}

/* ----------------------------- Size ----------------------------- */

/** @test size() returns sizeof wrapped type. */
TEST(ModelData, SizeReturnsCorrectValue) {
  EXPECT_EQ((ModelData<SimpleData, DataCategory::STATE>::size()), sizeof(SimpleData));
  EXPECT_EQ((ModelData<PhysicalConstants, DataCategory::STATIC_PARAM>::size()),
            sizeof(PhysicalConstants));
  EXPECT_EQ((ModelData<SensorReading, DataCategory::INPUT>::size()), sizeof(SensorReading));
}

/* ----------------------------- Type Aliases ----------------------------- */

/** @test StaticParam alias works correctly. */
TEST(ModelData, StaticParamAlias) {
  StaticParam<PhysicalConstants> constants{{9.81, 1.225}};

  EXPECT_EQ(constants.category(), DataCategory::STATIC_PARAM);
  EXPECT_TRUE(constants.isReadOnly());
  EXPECT_DOUBLE_EQ(constants->gravity, 9.81);
}

/** @test TunableParam alias works correctly. */
TEST(ModelData, TunableParamAlias) {
  TunableParam<ControlGains> gains{{1.0, 0.1, 0.01}};

  EXPECT_EQ(gains.category(), DataCategory::TUNABLE_PARAM);
  EXPECT_FALSE(gains.isReadOnly());

  gains->kp = 2.0;
  EXPECT_DOUBLE_EQ(gains->kp, 2.0);
}

/** @test State alias works correctly. */
TEST(ModelData, StateAlias) {
  State<SimpleData> state{{0}};

  EXPECT_EQ(state.category(), DataCategory::STATE);
  EXPECT_TRUE(state.isState());

  state->value = 42;
  EXPECT_EQ(state->value, 42u);
}

/** @test Input alias works correctly. */
TEST(ModelData, InputAlias) {
  Input<SensorReading> input{};

  EXPECT_EQ(input.category(), DataCategory::INPUT);
  EXPECT_TRUE(input.isModelInput());

  input->timestamp = 1000;
  EXPECT_EQ(input->timestamp, 1000u);
}

/** @test Output alias works correctly. */
TEST(ModelData, OutputAlias) {
  Output<SimpleData> output{};

  EXPECT_EQ(output.category(), DataCategory::OUTPUT);
  EXPECT_TRUE(output.isModelOutput());

  output->value = 123;
  EXPECT_EQ(output->value, 123u);
}

/* ----------------------------- Copy/Move ----------------------------- */

/** @test Non-STATIC ModelData can be copy assigned. */
TEST(ModelData, CopyAssignment) {
  TunableParam<ControlGains> gains1{{1.0, 0.1, 0.01}};
  TunableParam<ControlGains> gains2{{2.0, 0.2, 0.02}};

  gains1 = gains2;

  EXPECT_DOUBLE_EQ(gains1->kp, 2.0);
  EXPECT_DOUBLE_EQ(gains1->ki, 0.2);
  EXPECT_DOUBLE_EQ(gains1->kd, 0.02);
}

/** @test Non-STATIC ModelData can be move assigned. */
TEST(ModelData, MoveAssignment) {
  State<SimpleData> state1{{10}};
  State<SimpleData> state2{{20}};

  state1 = std::move(state2);

  EXPECT_EQ(state1->value, 20u);
}

/* ----------------------------- Compile-Time Checks ----------------------------- */

/** @test CATEGORY constant is accessible at compile time. */
TEST(ModelData, CategoryConstantIsConstexpr) {
  constexpr auto CAT = StaticParam<SimpleData>::CATEGORY;
  EXPECT_EQ(CAT, DataCategory::STATIC_PARAM);
}

/** @test ValueType alias is correct. */
TEST(ModelData, ValueTypeAlias) {
  using DataType = TunableParam<ControlGains>::ValueType;
  static_assert(std::is_same_v<DataType, ControlGains>);
  SUCCEED();
}
