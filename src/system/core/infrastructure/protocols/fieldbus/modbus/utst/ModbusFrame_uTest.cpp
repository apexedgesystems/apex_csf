/**
 * @file ModbusFrame_uTest.cpp
 * @brief Unit tests for Modbus frame building and parsing.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"

#include <gtest/gtest.h>

namespace modbus = apex::protocols::fieldbus::modbus;

/* ----------------------------- FrameBuffer ----------------------------- */

/** @test FrameBuffer default construction has zero length. */
TEST(FrameBufferTest, DefaultConstruction) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(buf.length, 0);
  EXPECT_EQ(buf.remaining(), modbus::FrameBuffer::CAPACITY);
}

/** @test FrameBuffer reset clears length. */
TEST(FrameBufferTest, Reset) {
  modbus::FrameBuffer buf;
  buf.append(0x01);
  buf.append(0x02);
  EXPECT_EQ(buf.length, 2);

  buf.reset();
  EXPECT_EQ(buf.length, 0);
}

/** @test FrameBuffer append single byte works. */
TEST(FrameBufferTest, AppendByte) {
  modbus::FrameBuffer buf;
  EXPECT_TRUE(buf.append(0xAB));
  EXPECT_EQ(buf.length, 1);
  EXPECT_EQ(buf.data[0], 0xAB);
}

/** @test FrameBuffer appendU16BE appends in big-endian order. */
TEST(FrameBufferTest, AppendU16BE) {
  modbus::FrameBuffer buf;
  EXPECT_TRUE(buf.appendU16BE(0x1234));
  EXPECT_EQ(buf.length, 2);
  EXPECT_EQ(buf.data[0], 0x12); // High byte first
  EXPECT_EQ(buf.data[1], 0x34); // Low byte second
}

/** @test FrameBuffer appendU16LE appends in little-endian order. */
TEST(FrameBufferTest, AppendU16LE) {
  modbus::FrameBuffer buf;
  EXPECT_TRUE(buf.appendU16LE(0x1234));
  EXPECT_EQ(buf.length, 2);
  EXPECT_EQ(buf.data[0], 0x34); // Low byte first
  EXPECT_EQ(buf.data[1], 0x12); // High byte second
}

/** @test FrameBuffer appendBytes works correctly. */
TEST(FrameBufferTest, AppendBytes) {
  modbus::FrameBuffer buf;
  const std::uint8_t DATA[] = {0xAA, 0xBB, 0xCC};
  EXPECT_TRUE(buf.appendBytes(DATA, sizeof(DATA)));
  EXPECT_EQ(buf.length, 3);
  EXPECT_EQ(buf.data[0], 0xAA);
  EXPECT_EQ(buf.data[1], 0xBB);
  EXPECT_EQ(buf.data[2], 0xCC);
}

/** @test FrameBuffer rejects append when full. */
TEST(FrameBufferTest, AppendOverflow) {
  modbus::FrameBuffer buf;
  buf.length = modbus::FrameBuffer::CAPACITY;
  EXPECT_FALSE(buf.append(0x00));
}

/* ----------------------------- CRC Helpers ----------------------------- */

/** @test calculateCrc matches known Modbus CRC test vector. */
TEST(ModbusCrcTest, KnownVector) {
  // Standard Modbus test: "123456789" -> 0x4B37
  const std::uint8_t DATA[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const std::uint16_t CRC = modbus::calculateCrc(DATA, sizeof(DATA));
  EXPECT_EQ(CRC, 0x4B37);
}

/** @test calculateCrc for a read holding registers request. */
TEST(ModbusCrcTest, ReadHoldingRegistersRequest) {
  // Request: Unit=1, FC=0x03, Addr=0x006B, Qty=0x0003
  const std::uint8_t DATA[] = {0x01, 0x03, 0x00, 0x6B, 0x00, 0x03};
  const std::uint16_t CRC = modbus::calculateCrc(DATA, sizeof(DATA));
  // CRC-16/MODBUS calculated value
  EXPECT_EQ(CRC, 0x1774);
}

/** @test verifyCrc returns true for valid frame. */
TEST(ModbusCrcTest, VerifyValid) {
  // Complete frame with valid CRC (little-endian: 0x74, 0x17)
  const std::uint8_t FRAME[] = {0x01, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x74, 0x17};
  EXPECT_TRUE(modbus::verifyCrc(FRAME, sizeof(FRAME)));
}

/** @test verifyCrc returns false for invalid frame. */
TEST(ModbusCrcTest, VerifyInvalid) {
  // Frame with wrong CRC
  const std::uint8_t FRAME[] = {0x01, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x00, 0x00};
  EXPECT_FALSE(modbus::verifyCrc(FRAME, sizeof(FRAME)));
}

/** @test verifyCrc returns false for too-short frame. */
TEST(ModbusCrcTest, VerifyTooShort) {
  const std::uint8_t FRAME[] = {0x01, 0x03};
  EXPECT_FALSE(modbus::verifyCrc(FRAME, sizeof(FRAME)));
}

/** @test verifyCrc returns false for null pointer. */
TEST(ModbusCrcTest, VerifyNull) { EXPECT_FALSE(modbus::verifyCrc(nullptr, 10)); }

/* ----------------------------- Request Building ----------------------------- */

/** @test buildReadHoldingRegistersRequest creates valid frame. */
TEST(ModbusFrameBuildTest, ReadHoldingRegisters) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildReadHoldingRegistersRequest(buf, 1, 0x006B, 3);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 8); // Unit + FC + Addr(2) + Qty(2) + CRC(2)

  EXPECT_EQ(buf.data[0], 0x01); // Unit address
  EXPECT_EQ(buf.data[1], 0x03); // Function code
  EXPECT_EQ(buf.data[2], 0x00); // Start address high
  EXPECT_EQ(buf.data[3], 0x6B); // Start address low
  EXPECT_EQ(buf.data[4], 0x00); // Quantity high
  EXPECT_EQ(buf.data[5], 0x03); // Quantity low

  // Verify CRC is valid
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildReadHoldingRegistersRequest rejects invalid unit address. */
TEST(ModbusFrameBuildTest, ReadHoldingRegistersInvalidUnit) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(modbus::buildReadHoldingRegistersRequest(buf, 248, 0, 1),
            modbus::Status::ERROR_INVALID_ARG);
}

/** @test buildReadHoldingRegistersRequest rejects zero quantity. */
TEST(ModbusFrameBuildTest, ReadHoldingRegistersZeroQty) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(modbus::buildReadHoldingRegistersRequest(buf, 1, 0, 0),
            modbus::Status::ERROR_INVALID_ARG);
}

/** @test buildReadHoldingRegistersRequest rejects excessive quantity. */
TEST(ModbusFrameBuildTest, ReadHoldingRegistersExcessiveQty) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(modbus::buildReadHoldingRegistersRequest(buf, 1, 0, 126),
            modbus::Status::ERROR_INVALID_ARG);
}

/** @test buildReadInputRegistersRequest creates valid frame. */
TEST(ModbusFrameBuildTest, ReadInputRegisters) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildReadInputRegistersRequest(buf, 1, 0x0000, 10);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 8);
  EXPECT_EQ(buf.data[1], 0x04); // Function code for Read Input Registers
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildReadCoilsRequest creates valid frame. */
TEST(ModbusFrameBuildTest, ReadCoils) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildReadCoilsRequest(buf, 1, 0x0013, 19);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 8);
  EXPECT_EQ(buf.data[1], 0x01); // Function code for Read Coils
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildWriteSingleCoilRequest creates valid frame for ON. */
TEST(ModbusFrameBuildTest, WriteSingleCoilOn) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildWriteSingleCoilRequest(buf, 1, 0x00AC, true);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 8);
  EXPECT_EQ(buf.data[1], 0x05); // Function code
  EXPECT_EQ(buf.data[4], 0xFF); // ON value high byte
  EXPECT_EQ(buf.data[5], 0x00); // ON value low byte
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildWriteSingleCoilRequest creates valid frame for OFF. */
TEST(ModbusFrameBuildTest, WriteSingleCoilOff) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildWriteSingleCoilRequest(buf, 1, 0x00AC, false);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.data[4], 0x00); // OFF value high byte
  EXPECT_EQ(buf.data[5], 0x00); // OFF value low byte
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildWriteSingleRegisterRequest creates valid frame. */
TEST(ModbusFrameBuildTest, WriteSingleRegister) {
  modbus::FrameBuffer buf;
  const modbus::Status S = modbus::buildWriteSingleRegisterRequest(buf, 1, 0x0001, 0x0003);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 8);
  EXPECT_EQ(buf.data[1], 0x06); // Function code
  EXPECT_EQ(buf.data[4], 0x00); // Value high byte
  EXPECT_EQ(buf.data[5], 0x03); // Value low byte
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildWriteMultipleRegistersRequest creates valid frame. */
TEST(ModbusFrameBuildTest, WriteMultipleRegisters) {
  modbus::FrameBuffer buf;
  const std::uint16_t VALUES[] = {0x000A, 0x0102};
  const modbus::Status S = modbus::buildWriteMultipleRegistersRequest(buf, 1, 0x0001, VALUES, 2);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(buf.length, 13);     // Unit + FC + Addr(2) + Qty(2) + ByteCount + Data(4) + CRC(2)
  EXPECT_EQ(buf.data[1], 0x10);  // Function code
  EXPECT_EQ(buf.data[6], 0x04);  // Byte count (2 registers * 2 bytes)
  EXPECT_EQ(buf.data[7], 0x00);  // First value high
  EXPECT_EQ(buf.data[8], 0x0A);  // First value low
  EXPECT_EQ(buf.data[9], 0x01);  // Second value high
  EXPECT_EQ(buf.data[10], 0x02); // Second value low
  EXPECT_TRUE(modbus::verifyCrc(buf.data, buf.length));
}

/** @test buildWriteMultipleRegistersRequest rejects null values. */
TEST(ModbusFrameBuildTest, WriteMultipleRegistersNull) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(modbus::buildWriteMultipleRegistersRequest(buf, 1, 0, nullptr, 1),
            modbus::Status::ERROR_INVALID_ARG);
}

/** @test Broadcast address (0) is accepted for write operations. */
TEST(ModbusFrameBuildTest, BroadcastAddress) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(modbus::buildWriteSingleCoilRequest(buf, 0, 0, true), modbus::Status::SUCCESS);
}

/* ----------------------------- Response Parsing ----------------------------- */

/** @test parseRtuResponse correctly parses a normal response. */
TEST(ModbusFrameParseTest, NormalResponse) {
  // Response: Unit=1, FC=0x03, ByteCount=6, Data=0x00 0x21 0x00 0x32 0x00 0x43, CRC
  std::uint8_t frame[] = {0x01, 0x03, 0x06, 0x00, 0x21, 0x00, 0x32, 0x00, 0x43, 0x00, 0x00};
  // Calculate and append correct CRC
  const std::uint16_t CRC = modbus::calculateCrc(frame, 9);
  frame[9] = static_cast<std::uint8_t>(CRC & 0xFF);
  frame[10] = static_cast<std::uint8_t>(CRC >> 8);

  modbus::ParsedResponse result;
  const modbus::Status S = modbus::parseRtuResponse(frame, sizeof(frame), result);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(result.unitAddress, 0x01);
  EXPECT_EQ(result.functionCode, 0x03);
  EXPECT_FALSE(result.isException);
  EXPECT_NE(result.data, nullptr);
  EXPECT_EQ(result.dataLength, 7); // ByteCount + 6 data bytes
}

/** @test parseRtuResponse correctly parses an exception response. */
TEST(ModbusFrameParseTest, ExceptionResponse) {
  // Exception response: Unit=1, FC=0x83, ExceptionCode=0x02, CRC
  std::uint8_t frame[] = {0x01, 0x83, 0x02, 0x00, 0x00};
  const std::uint16_t CRC = modbus::calculateCrc(frame, 3);
  frame[3] = static_cast<std::uint8_t>(CRC & 0xFF);
  frame[4] = static_cast<std::uint8_t>(CRC >> 8);

  modbus::ParsedResponse result;
  const modbus::Status S = modbus::parseRtuResponse(frame, sizeof(frame), result);
  EXPECT_EQ(S, modbus::Status::ERROR_EXCEPTION);
  EXPECT_EQ(result.unitAddress, 0x01);
  EXPECT_EQ(result.functionCode, 0x83);
  EXPECT_TRUE(result.isException);
  EXPECT_EQ(result.exceptionCode, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
}

/** @test parseRtuResponse rejects frame with bad CRC. */
TEST(ModbusFrameParseTest, BadCrc) {
  const std::uint8_t FRAME[] = {0x01, 0x03, 0x02, 0x00, 0x21, 0xFF, 0xFF};

  modbus::ParsedResponse result;
  const modbus::Status S = modbus::parseRtuResponse(FRAME, sizeof(FRAME), result);
  EXPECT_EQ(S, modbus::Status::ERROR_CRC);
}

/** @test parseRtuResponse rejects too-short frame. */
TEST(ModbusFrameParseTest, TooShort) {
  const std::uint8_t FRAME[] = {0x01, 0x03};

  modbus::ParsedResponse result;
  const modbus::Status S = modbus::parseRtuResponse(FRAME, sizeof(FRAME), result);
  EXPECT_EQ(S, modbus::Status::ERROR_FRAME);
}

/** @test parseRtuResponse rejects null frame. */
TEST(ModbusFrameParseTest, NullFrame) {
  modbus::ParsedResponse result;
  EXPECT_EQ(modbus::parseRtuResponse(nullptr, 10, result), modbus::Status::ERROR_INVALID_ARG);
}

/* ----------------------------- Data Extraction ----------------------------- */

/** @test extractRegisters correctly extracts register values. */
TEST(ModbusFrameParseTest, ExtractRegisters) {
  // Simulated data portion: [ByteCount=4] [Reg0=0x1234] [Reg1=0x5678]
  const std::uint8_t DATA[] = {0x04, 0x12, 0x34, 0x56, 0x78};
  modbus::ParsedResponse response;
  response.data = DATA;
  response.dataLength = sizeof(DATA);

  std::uint16_t values[10];
  std::size_t count = 0;
  const modbus::Status S = modbus::extractRegisters(response, values, 10, count);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(count, 2);
  EXPECT_EQ(values[0], 0x1234);
  EXPECT_EQ(values[1], 0x5678);
}

/** @test extractRegisters respects maxCount. */
TEST(ModbusFrameParseTest, ExtractRegistersMaxCount) {
  const std::uint8_t DATA[] = {0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03};
  modbus::ParsedResponse response;
  response.data = DATA;
  response.dataLength = sizeof(DATA);

  std::uint16_t values[2];
  std::size_t count = 0;
  const modbus::Status S = modbus::extractRegisters(response, values, 2, count);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(count, 2); // Extracted only 2 even though 3 available
  EXPECT_EQ(values[0], 0x0001);
  EXPECT_EQ(values[1], 0x0002);
}

/** @test extractCoils correctly extracts coil values. */
TEST(ModbusFrameParseTest, ExtractCoils) {
  // Simulated data: [ByteCount=2] [0b10101010] [0b01010101]
  const std::uint8_t DATA[] = {0x02, 0xAA, 0x55};
  modbus::ParsedResponse response;
  response.data = DATA;
  response.dataLength = sizeof(DATA);

  bool values[16];
  std::size_t count = 0;
  const modbus::Status S = modbus::extractCoils(response, values, 16, count);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(count, 16);

  // First byte 0xAA = 10101010: bits 1,3,5,7 are set
  EXPECT_FALSE(values[0]);
  EXPECT_TRUE(values[1]);
  EXPECT_FALSE(values[2]);
  EXPECT_TRUE(values[3]);
  EXPECT_FALSE(values[4]);
  EXPECT_TRUE(values[5]);
  EXPECT_FALSE(values[6]);
  EXPECT_TRUE(values[7]);

  // Second byte 0x55 = 01010101: bits 0,2,4,6 are set
  EXPECT_TRUE(values[8]);
  EXPECT_FALSE(values[9]);
  EXPECT_TRUE(values[10]);
  EXPECT_FALSE(values[11]);
  EXPECT_TRUE(values[12]);
  EXPECT_FALSE(values[13]);
  EXPECT_TRUE(values[14]);
  EXPECT_FALSE(values[15]);
}
