/**
 * @file RoverLidarModel_uTest.cpp
 * @brief The lidar model against a board-side receiver on a pseudo-terminal.
 *
 * A PtyPair stands in for the USB-serial adapter: the model opens the
 * slave end exactly as it opens /dev/ttyUSB0, and the master end feeds
 * the RoverBoardLidar the firmware runs, so these tests cover the scan
 * the sensor builds, the wire, and the picture the board reports.
 */

#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardLidar.hpp"
#include "demos/apex_horizon_demo/rover_mcu/lidar_model/inc/RoverLidarModel.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <thread>

using apex::protocols::serial::uart::PtyPair;
using appsim::ground_vehicle::GroundVehicleTelemetry;
using appsim::rover_board::BoardCommand;
using appsim::rover_board::LIDAR_NEAREST_NONE;
using appsim::rover_board::LIDAR_NEVER;
using appsim::rover_board::LIDAR_NO_RETURN_CM;
using appsim::rover_board::LIDAR_STALE;
using appsim::rover_board::LIDAR_STALE_MS;
using appsim::rover_board::LIDAR_UP;
using appsim::rover_board::LidarScan;
using appsim::rover_board::MAX_FRAME_PAYLOAD;
using appsim::rover_board::Opcode;
using appsim::rover_board::RoverBoardLidar;
using appsim::rover_board::RoverLidarModel;
using UartStatus = apex::protocols::serial::uart::Status;
namespace slip = apex::protocols::slip;

namespace {

GroundVehicleTelemetry sweep(std::initializer_list<double> ranges) {
  GroundVehicleTelemetry t{};
  t.lidar_n_rays = 8;
  std::size_t i = 0;
  for (double r : ranges) {
    t.lidar_range_m[i] = r;
    t.lidar_hit[i] = (r < 500.0) ? 1u : 0u;
    ++i;
  }
  for (; i < 8; ++i) {
    t.lidar_range_m[i] = 500.0;
  }
  return t;
}

/// The board's end of the sensor wire: decodes LIDAR_SCAN into RoverBoardLidar.
struct BoardSensorPort {
  PtyPair pty;
  RoverBoardLidar lidar;
  slip::DecodeState ds{};
  slip::DecodeConfig dc{};
  std::array<std::uint8_t, 512> raw{};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> dec{};
  LidarScan last{};
  int scans{0};

  BoardSensorPort() {
    dc.maxFrameSize = MAX_FRAME_PAYLOAD;
    dc.allowEmptyFrame = false;
    dc.dropUntilEnd = true;
    dc.requireTrailingEnd = true;
  }

  void service(std::uint32_t now_ms) {
    for (int guard = 0; guard < 8; ++guard) {
      std::size_t n = 0;
      if (pty.readMaster(raw.data(), raw.size(), n, 20) != UartStatus::SUCCESS || n == 0u) {
        return;
      }
      std::size_t pos = 0;
      while (pos < n) {
        const std::size_t PREV = ds.frameLen;
        auto r = slip::decodeChunk(ds, dc, {raw.data() + pos, n - pos}, dec.data() + PREV,
                                   dec.size() - PREV);
        pos += r.bytesConsumed;
        if (r.frameCompleted) {
          const auto F = appsim::rover_board::parseFrame(dec.data(), PREV + r.bytesProduced);
          if (F.ok && F.opcode == Opcode::LIDAR_SCAN && F.payload_len == sizeof(LidarScan)) {
            std::memcpy(&last, F.payload, sizeof(last));
            lidar.onScan(last, now_ms);
            ++scans;
          } else {
            lidar.onBadFrame();
          }
        }
        if (r.bytesConsumed == 0u) {
          break;
        }
      }
    }
  }
};

void openOn(RoverLidarModel& model, const PtyPair& pty, float max_range_m) {
  auto& p = model.tunables().get();
  std::strncpy(p.device_path, pty.slavePath(), sizeof(p.device_path) - 1);
  p.enabled = 1;
  p.max_range_m = max_range_m;
  (void)model.telemetryTick(); // opens the port
}

} // namespace

/** @test A sweep becomes one scan: returns inside the range set bits and ranges, the rest none. */
TEST(RoverLidarModel, ScanKeepsOnlyReturnsInsideTheSensorRange) {
  const auto TLM = sweep({500.0, 45.0, 25.0, 12.5, 60.0, 500.0, 30.0, 49.99});
  const LidarScan S = RoverLidarModel::scanFrom(TLM, 50.0F, 7);
  EXPECT_EQ(S.scan_seq, 7u);
  EXPECT_EQ(S.n_rays, 8u);
  EXPECT_EQ(S.hit_bits, 0b11001110u); // rays 1, 2, 3, 6, 7; ray 4 is past the range
  EXPECT_EQ(S.range_cm[0], LIDAR_NO_RETURN_CM);
  EXPECT_EQ(S.range_cm[1], 4500u);
  EXPECT_EQ(S.range_cm[3], 1250u);
  EXPECT_EQ(S.range_cm[4], LIDAR_NO_RETURN_CM); // 60 m is past a 50 m sensor
  EXPECT_EQ(S.range_cm[7], 4999u);
}

/** @test Scans cross the wire and the board reports state, number, bits and the closest return. */
TEST(RoverLidarModel, BoardReportsWhatCrossedTheWire) {
  BoardSensorPort board;
  ASSERT_EQ(board.pty.open(), UartStatus::SUCCESS);
  RoverLidarModel model;
  auto tlm = sweep({500.0, 45.0, 25.0, 12.5, 60.0, 500.0, 30.0, 40.0});
  model.setSource(&tlm);
  openOn(model, board.pty, 50.0F);
  ASSERT_EQ(model.modelState().uart_open, 1u);

  BoardCommand before{};
  board.lidar.fill(before, 0);
  EXPECT_EQ(before.lidar_state, LIDAR_NEVER);
  EXPECT_EQ(before.lidar_nearest_m, LIDAR_NEAREST_NONE);

  for (int i = 0; i < 3; ++i) {
    (void)model.scanStep();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    board.service(1000u + static_cast<std::uint32_t>(i) * 100u);
  }
  EXPECT_EQ(board.scans, 3);
  EXPECT_EQ(board.lidar.badFrames(), 0u);
  EXPECT_EQ(model.modelState().scans_sent, 3u);

  BoardCommand cmd{};
  board.lidar.fill(cmd, 1250u);
  EXPECT_EQ(cmd.lidar_state, LIDAR_UP);
  EXPECT_EQ(cmd.lidar_scan_seq, 3u);
  EXPECT_EQ(cmd.lidar_hit_bits, 0b11001110u);
  EXPECT_EQ(cmd.lidar_nearest_m, 12u);

  // The plant's sweep changes: the next scan carries it.
  tlm = sweep({500.0, 500.0, 500.0, 500.0, 500.0, 500.0, 500.0, 500.0});
  (void)model.scanStep();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  board.service(1300u);
  board.lidar.fill(cmd, 1300u);
  EXPECT_EQ(cmd.lidar_hit_bits, 0u);
  EXPECT_EQ(cmd.lidar_nearest_m, LIDAR_NEAREST_NONE);
  EXPECT_EQ(cmd.lidar_scan_seq, 4u);

  // Scans stop: the board calls the sensor stale after LIDAR_STALE_MS.
  board.lidar.fill(cmd, 1300u + LIDAR_STALE_MS);
  EXPECT_EQ(cmd.lidar_state, LIDAR_UP);
  board.lidar.fill(cmd, 1301u + LIDAR_STALE_MS);
  EXPECT_EQ(cmd.lidar_state, LIDAR_STALE);
}

/** @test Disabled, the model never opens its port and sends nothing. */
TEST(RoverLidarModel, DisabledNeverOpens) {
  BoardSensorPort board;
  ASSERT_EQ(board.pty.open(), UartStatus::SUCCESS);
  RoverLidarModel model;
  auto tlm = sweep({10.0});
  model.setSource(&tlm);
  auto& p = model.tunables().get();
  std::strncpy(p.device_path, board.pty.slavePath(), sizeof(p.device_path) - 1);
  p.enabled = 0;
  (void)model.telemetryTick();
  (void)model.scanStep();
  board.service(0);
  EXPECT_EQ(model.modelState().uart_open, 0u);
  EXPECT_EQ(board.scans, 0);
}

/** @test A corrupted scan on the sensor wire is counted and leaves the board's picture alone. */
TEST(RoverLidarModel, CorruptScanIsRefused) {
  BoardSensorPort board;
  ASSERT_EQ(board.pty.open(), UartStatus::SUCCESS);
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> frame{};
  std::array<std::uint8_t, 2 * MAX_FRAME_PAYLOAD + 2> enc{};
  LidarScan scan{};
  scan.n_rays = 8;
  scan.hit_bits = 0xFF;
  const std::size_t N = appsim::rover_board::buildFrame(Opcode::LIDAR_SCAN, &scan, sizeof(scan),
                                                        frame.data(), frame.size());
  frame[5] ^= 0x40u; // a bit flipped on the wire after the CRC was computed
  auto e = slip::encode({frame.data(), N}, enc.data(), enc.size());
  // The board's decoder reads the master end; a write on the slave end reaches it.
  apex::protocols::serial::uart::UartAdapter sensor{std::string(board.pty.slavePath())};
  apex::protocols::serial::uart::UartConfig cfg;
  ASSERT_EQ(sensor.configure(cfg), UartStatus::SUCCESS);
  std::size_t written = 0;
  ASSERT_EQ(sensor.write(enc.data(), e.bytesProduced, written, 100), UartStatus::SUCCESS);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  board.service(0);
  BoardCommand cmd{};
  board.lidar.fill(cmd, 0);
  EXPECT_EQ(board.scans, 0);
  EXPECT_EQ(board.lidar.badFrames(), 1u);
  EXPECT_EQ(cmd.lidar_state, LIDAR_NEVER);
}
