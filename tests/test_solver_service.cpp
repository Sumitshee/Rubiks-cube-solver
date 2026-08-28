#include "core/Cube.h"
#include "core/Move.h"
#include "solver/MoveTable.h"
#include "solver/TwoPhaseSolver.h"
#include "solver/korf/KorfHeuristic.h"
#include "ui/CubeController.h"
#include "ui/SolverService.h"

#include "TestDatabases.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace rubik;
using namespace rubik::ui;

namespace {

std::shared_ptr<const TwoPhaseTables> twoPhaseTables() {
  static const std::shared_ptr<const TwoPhaseTables> tables =
      std::make_shared<const TwoPhaseTables>();
  return tables;
}

/// The strongest configuration on disk; null when the databases are absent.
/// Shared with the rest of the binary; see tests/TestDatabases.h.
std::shared_ptr<const korf::KorfHeuristic> korfHeuristic() {
  return testdb::bestHeuristic();
}

/// Polls as the render loop would, without blocking on the worker.
std::optional<SolveReport> waitForReport(SolverService& service,
                                         std::chrono::seconds limit) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto report = service.takeReport()) return report;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return std::nullopt;
}

}  // namespace

TEST(SolverService, KociembaSolvesAndVerifiesOnAWorkerThread) {
  SolverService service(twoPhaseTables(), korfHeuristic());

  Cube cube;
  (void)cube.scramble(25, 1234);

  service.start(SolverChoice::Kociemba, cube, 1, std::chrono::seconds(10));
  // The caller stays responsive: nothing here blocks on the solver.
  EXPECT_FALSE(service.statusLine().empty());

  const auto report = waitForReport(service, std::chrono::seconds(30));
  ASSERT_TRUE(report.has_value()) << "the solve never reported back";
  EXPECT_TRUE(report->solved);
  EXPECT_TRUE(report->verified);
  EXPECT_FALSE(report->optimal) << "Kociemba must not claim optimality";
  EXPECT_GT(report->length, 0);

  Cube check = cube;
  check.apply(report->moves);
  EXPECT_TRUE(check.isSolved());
}

TEST(SolverService, KorfSolvesOptimallyAndVerifies) {
  if (!korfHeuristic()) {
    GTEST_SKIP() << "pattern databases absent; run rubiks_solver --generate-pdb";
  }
  SolverService service(twoPhaseTables(), korfHeuristic());
  ASSERT_TRUE(service.korfAvailable());

  Cube cube;
  const auto scramble = cube.scramble(8, 555);

  service.start(SolverChoice::Korf, cube, 4, std::chrono::seconds(60));
  const auto report = waitForReport(service, std::chrono::seconds(120));

  ASSERT_TRUE(report.has_value());
  EXPECT_TRUE(report->solved);
  EXPECT_TRUE(report->verified);
  EXPECT_TRUE(report->optimal);
  EXPECT_EQ(report->threads, 4);
  EXPECT_GE(report->initialHeuristic, 0);
  EXPECT_LE(report->length, static_cast<int>(scramble.size()));

  Cube check = cube;
  check.apply(report->moves);
  EXPECT_TRUE(check.isSolved());
}

TEST(SolverService, RunsAtEveryThreadCount) {
  if (!korfHeuristic()) GTEST_SKIP() << "pattern databases absent";
  SolverService service(twoPhaseTables(), korfHeuristic());

  int reference = -1;
  for (const int threads : {1, 4, 8}) {
    Cube cube;
    (void)cube.scramble(8, 8080);

    service.start(SolverChoice::Korf, cube, threads, std::chrono::seconds(60));
    const auto report = waitForReport(service, std::chrono::seconds(120));
    ASSERT_TRUE(report.has_value()) << threads << " threads";
    ASSERT_TRUE(report->verified) << threads << " threads";

    if (reference < 0) {
      reference = report->length;
    } else {
      EXPECT_EQ(report->length, reference)
          << threads << " threads gave a different optimal length";
    }
  }
}

TEST(SolverService, CancellationStopsTheSearchWithoutClaimingASolution) {
  if (!korfHeuristic()) GTEST_SKIP() << "pattern databases absent";
  SolverService service(twoPhaseTables(), korfHeuristic());

  Cube cube;
  (void)cube.scramble(40, 4321);  // deep enough to run for a long time

  service.start(SolverChoice::Korf, cube, 4, std::chrono::seconds(600));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  service.cancel();

  const auto report = waitForReport(service, std::chrono::seconds(60));
  ASSERT_TRUE(report.has_value()) << "cancellation never reported back";
  EXPECT_FALSE(report->optimal) << "a cancelled search must not claim optimality";
  EXPECT_FALSE(report->solved);
  EXPECT_NE(report->status.find("cancelled"), std::string::npos) << report->status;
}

TEST(SolverService, DestructionDuringASolveDoesNotHang) {
  // The window closing mid-search must not wedge the process.
  if (!korfHeuristic()) GTEST_SKIP() << "pattern databases absent";
  const auto start = std::chrono::steady_clock::now();
  {
    SolverService service(twoPhaseTables(), korfHeuristic());
    Cube cube;
    (void)cube.scramble(40, 777);
    service.start(SolverChoice::Korf, cube, 4, std::chrono::seconds(600));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // The destructor cancels and joins.
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  EXPECT_LT(seconds, 20.0) << "shutting down took " << seconds << " s";
}

TEST(SolverService, SecondRequestWhileBusyIsIgnored) {
  SolverService service(twoPhaseTables(), korfHeuristic());
  Cube cube;
  (void)cube.scramble(25, 246);

  service.start(SolverChoice::Kociemba, cube, 1, std::chrono::seconds(10));
  service.start(SolverChoice::Kociemba, cube, 1, std::chrono::seconds(10));

  const auto report = waitForReport(service, std::chrono::seconds(30));
  ASSERT_TRUE(report.has_value());
  EXPECT_TRUE(report->verified);
  // Exactly one report, so nothing is left over for a second take.
  EXPECT_FALSE(service.takeReport().has_value());
}

TEST(SolverService, RepeatedSolvesDoNotRaceOrCorrupt) {
  // A stress run: many solves in sequence through one service, checking each.
  SolverService service(twoPhaseTables(), korfHeuristic());
  for (std::uint64_t seed = 0; seed < 12; ++seed) {
    Cube cube;
    (void)cube.scramble(25, seed);
    service.start(SolverChoice::Kociemba, cube, 1, std::chrono::seconds(10));
    const auto report = waitForReport(service, std::chrono::seconds(30));
    ASSERT_TRUE(report.has_value()) << "seed " << seed;
    ASSERT_TRUE(report->verified) << "seed " << seed;

    Cube check = cube;
    check.apply(report->moves);
    ASSERT_TRUE(check.isSolved()) << "seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// The whole loop: scramble, solve in the background, play the solution back.
// ---------------------------------------------------------------------------

TEST(SolverService, ScrambleSolveAndPlaybackEndsSolved) {
  SolverService service(twoPhaseTables(), korfHeuristic());
  CubeController controller;
  controller.setSpeed(25.0);

  (void)controller.scramble(22, 90210);
  while (controller.busy()) controller.update(1.0 / 60.0);
  ASSERT_FALSE(controller.cube().isSolved());

  // The solve runs on the worker while the "render loop" keeps ticking.
  service.start(SolverChoice::Kociemba, controller.cube(), 1,
                std::chrono::seconds(10));

  std::optional<SolveReport> report;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!report && std::chrono::steady_clock::now() < deadline) {
    controller.update(1.0 / 60.0);  // the loop stays live during the solve
    report = service.takeReport();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(report.has_value());
  ASSERT_TRUE(report->verified);

  controller.loadSolution(report->moves);
  controller.play();
  int frames = 0;
  while ((controller.playing() || controller.busy()) && frames < 40000) {
    controller.update(1.0 / 60.0);
    ++frames;
  }
  ASSERT_LT(frames, 40000);
  EXPECT_TRUE(controller.cube().isSolved())
      << "the cube was not solved after playing the solution back";
}

TEST(SolverService, KorfReportsAreConsistentWithTheControllerState) {
  if (!korfHeuristic()) GTEST_SKIP() << "pattern databases absent";
  SolverService service(twoPhaseTables(), korfHeuristic());
  CubeController controller;
  controller.setSpeed(25.0);

  (void)controller.scramble(7, 13579);
  while (controller.busy()) controller.update(1.0 / 60.0);

  service.start(SolverChoice::Korf, controller.cube(), 4,
                std::chrono::seconds(60));
  const auto report = waitForReport(service, std::chrono::seconds(120));
  ASSERT_TRUE(report.has_value());
  ASSERT_TRUE(report->optimal);

  controller.loadSolution(report->moves);
  controller.play();
  int frames = 0;
  while ((controller.playing() || controller.busy()) && frames < 40000) {
    controller.update(1.0 / 60.0);
    ++frames;
  }
  EXPECT_TRUE(controller.cube().isSolved());
  EXPECT_EQ(controller.solutionProgress(), report->length);
}
