/**
 * @file SchedulerTableSequencing_uTest.cpp
 * @brief What a task table's sequencing columns and worker count come to.
 *
 * A table row can ask for a task to be sequenced, but the sequencing is
 * the component's: the row takes effect only when the component created
 * that group and registered that task in it. The scheduler's startup
 * checks say what the loaded table comes to.
 *
 * Coverage:
 *  - A row sequencing a task in a group its component lacks, or in a
 *    group that task is not part of, leaves the task unsequenced and
 *    fails the preflight's sequence-reference check, with the count; a
 *    row the component honours passes it.
 *  - The contention check reports two unsequenced tasks of one component
 *    on one tick when the pool has two workers, and does not when it has
 *    one, where the tasks run one after the other.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using system_core::scheduler::NO_SEQUENCE_GROUP;
using system_core::scheduler::PreflightVerdict;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::SchedulerTaskEntry;
using system_core::scheduler::SchedulerTprmHeader;
using system_core::system_component::SwModelBase;
using system_core::system_component::SystemComponentBase;
using system_core::system_component::TprmIngest;

namespace {

constexpr std::uint32_t MODEL_UID = 0x00C800;
constexpr std::uint32_t SCHEDULER_UID = 0x000100;
constexpr std::uint8_t STEP_UID = 1;
constexpr std::uint8_t REPORT_UID = 2;
constexpr std::uint8_t LOOSE_UID = 3;

/// A model with two tasks in sequence group 0 and one task in no group.
class Model : public SwModelBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 200; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Model"; }
  [[nodiscard]] const char* label() const noexcept override { return "MODEL"; }

  [[nodiscard]] std::uint8_t noop() noexcept { return 0; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    const bool OK = createSequenceGroup(0, 2) &&
                    registerSequencedTask<Model, &Model::noop>(STEP_UID, this, "step", 0, 1) &&
                    registerSequencedTask<Model, &Model::noop>(REPORT_UID, this, "report", 0, 2) &&
                    registerTask<Model, &Model::noop>(LOOSE_UID, this, "loose");
    return OK ? 0 : 1;
  }
};

/// Resolves the one model the tables below name.
class OneModel : public system_core::system_component::IComponentResolver {
public:
  explicit OneModel(Model& model) noexcept : model_(model) {}

  [[nodiscard]] SystemComponentBase* getComponent(std::uint32_t fullUid) noexcept override {
    return fullUid == MODEL_UID ? &model_ : nullptr;
  }
  [[nodiscard]] const SystemComponentBase*
  getComponent(std::uint32_t fullUid) const noexcept override {
    return fullUid == MODEL_UID ? &model_ : nullptr;
  }

private:
  Model& model_;
};

/// One row of the model's: TASK at 100 Hz on tick 0 of pool 0, in GROUP.
SchedulerTaskEntry row(std::uint8_t task, std::uint8_t group) {
  return SchedulerTaskEntry{MODEL_UID, task, 0, 100, 1, 0, 0, group, 0};
}

/// A directory of this process's own for one test's files.
std::filesystem::path testDir(const char* name) {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_table_seq_utest_" + std::to_string(::getpid())) / name;
  std::filesystem::remove_all(DIR);
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// Write the scheduler's parameter file into DIR: WORKERS per pool, ROWS.
void writeTable(const std::filesystem::path& dir, std::uint8_t workers,
                const std::vector<SchedulerTaskEntry>& rows) {
  namespace sc = system_core::system_component;
  std::vector<std::uint8_t> body(sizeof(SchedulerTprmHeader) +
                                 rows.size() * sizeof(SchedulerTaskEntry));
  const SchedulerTprmHeader HEADER{1, workers, static_cast<std::uint8_t>(rows.size())};
  std::memcpy(body.data(), &HEADER, sizeof(HEADER));
  if (!rows.empty()) {
    std::memcpy(body.data() + sizeof(HEADER), rows.data(),
                rows.size() * sizeof(SchedulerTaskEntry));
  }

  sc::TprmPayloadHeader prelude{};
  prelude.magic = sc::TPRM_PAYLOAD_MAGIC;
  prelude.version = sc::TPRM_PAYLOAD_VERSION;
  prelude.fullUid = SCHEDULER_UID;
  prelude.payloadSize = body.size();
  prelude.payloadCrc = sc::tprmCrc32(body.data(), body.size());

  std::ofstream out(dir / SystemComponentBase::tprmFilename(SCHEDULER_UID),
                    std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&prelude), sizeof(prelude));
  out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
  ASSERT_TRUE(out.good());
}

/// The scheduler's log in DIR, whole.
std::string schedulerLog(const std::filesystem::path& dir) {
  std::ifstream in(dir / system_core::scheduler::SCHED_LOG_FN);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

/// A scheduler that loaded ROWS for WORKERS per pool, as the executive
/// loads a table: the table first, the log and the pools at init.
struct Loaded {
  Loaded(const char* name, std::uint8_t workers, const std::vector<SchedulerTaskEntry>& rows)
      : dir(testDir(name)), resolver(model), sched(100, dir) {
    EXPECT_EQ(model.init(), 0U);
    writeTable(dir, workers, rows);
    sched.setComponentResolver(&resolver);
    EXPECT_EQ(sched.loadTprm(dir), TprmIngest::LOADED);
    EXPECT_EQ(sched.init(), 0U);
  }
  ~Loaded() { sched.shutdown(); }

  std::filesystem::path dir;
  Model model;
  OneModel resolver;
  SchedulerMultiThread sched;
};

} // namespace

/* ----------------------------- Sequence references ----------------------------- */

/** @test A row the component sequences is sequenced and passes the check. */
TEST(SchedulerTableSequencingTest, ARowTheComponentHonoursIsSequenced) {
  Loaded l("honoured", 2,
           {row(STEP_UID, 0), row(REPORT_UID, 0), row(LOOSE_UID, NO_SEQUENCE_GROUP)});

  ASSERT_EQ(l.sched.entries().size(), 3U);
  EXPECT_TRUE(l.sched.entries()[0].isSequenced());
  EXPECT_TRUE(l.sched.entries()[1].isSequenced());
  EXPECT_FALSE(l.sched.entries()[2].isSequenced());
  EXPECT_FALSE(l.sched.entries()[2].seqRequested);

  EXPECT_EQ(l.sched.tablePreflight().seqRefs, PreflightVerdict::PASS);
  EXPECT_EQ(l.sched.tablePreflight().unsequencedTasks, 0U);
}

/**
 * @test A row naming a group the component lacks, and one naming a group
 *       its task is not part of, both run unsequenced and fail the check.
 */
TEST(SchedulerTableSequencingTest, ARowTheComponentDoesNotHonourFailsThePreflight) {
  Loaded l("unmet", 2, {row(STEP_UID, 1), row(LOOSE_UID, 0), row(REPORT_UID, 0)});

  ASSERT_EQ(l.sched.entries().size(), 3U);
  EXPECT_FALSE(l.sched.entries()[0].isSequenced()) << "group 1 does not exist";
  EXPECT_FALSE(l.sched.entries()[1].isSequenced()) << "the loose task is in no group";
  EXPECT_TRUE(l.sched.entries()[2].isSequenced());

  const auto& P = l.sched.tablePreflight();
  EXPECT_EQ(P.seqRefs, PreflightVerdict::FAIL);
  EXPECT_EQ(P.unsequencedTasks, 2U);
  EXPECT_EQ(P.overall(), PreflightVerdict::FAIL);

  // The table loaded before the log existed; the verdict is in it all the same.
  const std::string LOG = schedulerLog(l.dir);
  EXPECT_NE(LOG.find("[FAIL] sequence references: 2 task(s)"), std::string::npos) << LOG;
  EXPECT_NE(LOG.find("preflight verdict: FAIL"), std::string::npos);
}

/* ----------------------------- Contention check ----------------------------- */

/** @test Two unsequenced tasks of one component on one tick: two workers. */
TEST(SchedulerTableSequencingTest, ContentionIsReportedWhereTwoWorkersCanOverlap) {
  Loaded l("two_workers", 2,
           {row(STEP_UID, NO_SEQUENCE_GROUP), row(REPORT_UID, NO_SEQUENCE_GROUP)});

  const std::string LOG = schedulerLog(l.dir);
  EXPECT_NE(LOG.find("CONTENTION: 2 tasks from component 0x00C800"), std::string::npos) << LOG;
  EXPECT_EQ(LOG.find("No intra-component contention detected."), std::string::npos);
}

/** @test The same table on one worker: the tasks cannot overlap. */
TEST(SchedulerTableSequencingTest, NoContentionIsReportedOnOneWorker) {
  Loaded l("one_worker", 1, {row(STEP_UID, NO_SEQUENCE_GROUP), row(REPORT_UID, NO_SEQUENCE_GROUP)});

  const std::string LOG = schedulerLog(l.dir);
  EXPECT_EQ(LOG.find("CONTENTION:"), std::string::npos) << LOG;
  EXPECT_NE(LOG.find("No intra-component contention detected."), std::string::npos);
}

/** @test Sequenced together, the two tasks are not reported on two workers. */
TEST(SchedulerTableSequencingTest, NoContentionIsReportedForASequencedPair) {
  Loaded l("sequenced_pair", 2, {row(STEP_UID, 0), row(REPORT_UID, 0)});

  const std::string LOG = schedulerLog(l.dir);
  EXPECT_EQ(LOG.find("CONTENTION:"), std::string::npos) << LOG;
}
