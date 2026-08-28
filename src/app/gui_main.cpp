/// The 3D viewer.
///
/// This file is the application controller and nothing else: it wires input to
/// the `CubeController`, hands the cube to the renderer, and polls the
/// `SolverService` for results. It contains no solving logic and no drawing
/// code -- both live behind the interfaces it calls.
///
///     input  ->  ui::CubeController  ->  render::CubeRenderer
///                       ^        |                ^
///                       |        v                |
///     ui::CubeEditor ---+   ui::SolverService -- solution moves
///          |
///     rubik::diagnose (core)  -- rejects impossible cubes before solving
///
/// The solver targets (`rubiks_solver`, `rubiks_bench`) do not link against any
/// of the render code, so a machine with no display still builds and runs them.

#include "core/Cube.h"
#include "core/Error.h"
#include "core/Move.h"
#include "render/Camera.h"
#include "render/CubeRenderer.h"
#include "render/GlContext.h"
#include "render/TextOverlay.h"
#include "solver/MoveTable.h"
#include "solver/TwoPhaseTables.h"
#include "solver/korf/KorfHeuristic.h"
#include "core/CubeValidation.h"
#include "ui/CubeController.h"
#include "ui/CubeEditor.h"
#include "ui/CubeLayout.h"
#include "ui/SolverService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace rubik;
namespace rr = rubik::render;

constexpr int kMaxWidth = 1400;
constexpr int kMaxHeight = 900;
constexpr int kDefaultScramble = 25;
constexpr int kMinScramble = 4;
constexpr int kMaxScramble = 30;
/// Long enough for the optimal solver to finish most positions it can finish
/// at all, short enough that a hopeless one gives up rather than hanging the
/// window until the user kills it. Cancellable at any point with Escape.
constexpr auto kKorfTimeLimit = std::chrono::minutes(5);

/// Playback speeds, in turns per second, with the labels shown in the HUD.
///
/// Somebody copying moves onto a cube in their hands needs time to find the
/// face, turn it, and look back at the screen. Roughly one turn a second is
/// already brisk for that, which is where the slow end sits; the fast end is
/// for watching a solution rather than following one.
struct SpeedPreset {
  double movesPerSecond;
  const char* label;
};

constexpr std::array<SpeedPreset, 4> kSpeedPresets{{
    {0.6, "0.5x"},
    {1.2, "1x"},
    {2.4, "2x"},
    {4.8, "4x"},
}};

/// 1x -- the default whenever a solution is loaded for a hand-entered cube.
constexpr int kFollowSpeedIndex = 1;
/// 4x -- for watching a generated scramble get solved.
constexpr int kWatchSpeedIndex = 3;

/// Nearly opaque: a translucent panel is unreadable when a red cube face
/// happens to be behind it.
const glm::vec4 kPanelColour{0.04f, 0.05f, 0.08f, 0.9f};

/// The six sticker colours, in the Face order U, R, F, D, L, B. Deliberately
/// the same values the 3D renderer uses, so a sticker is the same colour in the
/// net editor and on the cube.
constexpr std::array<glm::vec3, kNumFaces> kStickerColours{{
    {0.94f, 0.94f, 0.94f},  // U -- white
    {0.78f, 0.13f, 0.16f},  // R -- red
    {0.10f, 0.56f, 0.28f},  // F -- green
    {0.96f, 0.80f, 0.11f},  // D -- yellow
    {0.90f, 0.42f, 0.09f},  // L -- orange
    {0.09f, 0.32f, 0.68f},  // B -- blue
}};

const char* const kControls =
    "  Mouse drag      orbit\n"
    "  Mouse wheel     zoom\n"
    "  C               reset camera\n"
    "\n"
    "  U R F D L B     quarter turn (B steps back\n"
    "                  while a solution is loaded)\n"
    "  + Shift         counter-clockwise\n"
    "  + Ctrl          half turn\n"
    "\n"
    "  E               enter my own cube\n"
    "  Space           scramble\n"
    "  [ / ]           scramble length\n"
    "  Backspace       reset cube\n"
    "  Tab             switch solver\n"
    "  Enter           solve\n"
    "  Esc             cancel solve / quit\n"
    "\n"
    "  Right / N       next move\n"
    "  Left  / B       previous move\n"
    "  P               play / pause\n"
    "  Home            restart solution\n"
    "  - / =           speed preset\n"
    "  Ctrl+C          copy the solution\n"
    "  1 2 4 8         solver threads\n"
    "  H               hide this panel";

const char* const kEditorControls =
    "  Click a colour   pick it up\n"
    "  Click a sticker  paint it\n"
    "  Arrows           move cursor\n"
    "  U R F D L B      paint at cursor\n"
    "  1 2 3 4 5 6      same, by number\n"
    "\n"
    "  Backspace        reset to solved\n"
    "  Delete           clear stickers\n"
    "  Enter            validate + use\n"
    "  Esc / E          leave editor";

/// A window that fits on the display it opens on.
///
/// A fixed size is fine until it is larger than somebody's screen, at which
/// point the bottom of the heads-up display is simply not there. GLFW reports
/// the monitor's work area -- the desktop minus the taskbar -- so the window
/// can be capped to fit it.
void chooseWindowSize(int& width, int& height) {
  width = kMaxWidth;
  height = kMaxHeight;

  if (glfwInit() != GLFW_TRUE) return;  // GlContext will report the failure
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor == nullptr) return;

  int x = 0;
  int y = 0;
  int areaWidth = 0;
  int areaHeight = 0;
  glfwGetMonitorWorkarea(monitor, &x, &y, &areaWidth, &areaHeight);
  if (areaWidth <= 0 || areaHeight <= 0) return;

  width = std::min(kMaxWidth, static_cast<int>(areaWidth * 0.9));
  // Leave room for the title bar, which is outside the size asked for.
  height = std::min(kMaxHeight, static_cast<int>(areaHeight * 0.88));
}

/// Loads the solver tables off the render thread.
///
/// Constructing the two-phase tables takes a moment and the pattern databases
/// are hundreds of megabytes; doing either on the render thread would freeze
/// the window before it had drawn a frame. The cube is interactive immediately
/// and the solvers light up as they become available.
class ResourceLoader {
 public:
  ~ResourceLoader() {
    if (thread_.joinable()) thread_.join();
  }

  void start(std::string directory, bool withSeven) {
    thread_ = std::thread([this, directory, withSeven] {
      setStatus("building two-phase tables");
      auto twoPhase = std::make_shared<const TwoPhaseTables>();

      setStatus("loading pattern databases");
      auto korf = std::make_shared<korf::KorfHeuristic>(twoPhase->moves());
      const bool loaded = korf->corners().load(directory + "/corner.db") &&
                          korf->edgesA().load(directory + "/edge_a.db") &&
                          korf->edgesB().load(directory + "/edge_b.db");
      if (loaded && withSeven) (void)korf->loadSeven(directory);

      {
        const std::lock_guard<std::mutex> lock(mutex_);
        twoPhase_ = std::move(twoPhase);
        korf_ = loaded ? std::shared_ptr<const korf::KorfHeuristic>(std::move(korf))
                       : nullptr;
        korfMissing_ = !loaded;
        status_.clear();
      }
      done_.store(true);
    });
  }

  [[nodiscard]] bool done() const { return done_.load(); }

  [[nodiscard]] std::string status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_;
  }

  /// Only valid once `done()`.
  void take(std::shared_ptr<const TwoPhaseTables>& twoPhase,
            std::shared_ptr<const korf::KorfHeuristic>& korf, bool& korfMissing) {
    if (thread_.joinable()) thread_.join();
    const std::lock_guard<std::mutex> lock(mutex_);
    twoPhase = twoPhase_;
    korf = korf_;
    korfMissing = korfMissing_;
  }

 private:
  void setStatus(std::string text) {
    const std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(text);
  }

  std::thread thread_;
  std::atomic<bool> done_{false};
  mutable std::mutex mutex_;
  std::string status_;
  std::shared_ptr<const TwoPhaseTables> twoPhase_;
  std::shared_ptr<const korf::KorfHeuristic> korf_;
  bool korfMissing_ = false;
};

[[nodiscard]] std::string formatDouble(double value, int decimals) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(decimals) << value;
  return out.str();
}

/// Groups a large count so a node total is readable at a glance.
[[nodiscard]] std::string formatCount(std::uint64_t value) {
  std::string digits = std::to_string(value);
  std::string out;
  int since = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (since == 3) {
      out.push_back(' ');
      since = 0;
    }
    out.push_back(*it);
    ++since;
  }
  return std::string(out.rbegin(), out.rend());
}

/// What the window is doing. The editor is a mode rather than a separate
/// window: the 3D cube stays on screen behind the net, so the user can see the
/// cube they are describing.
enum class Mode { Demo, Editor };

class Application {
 public:
  Application(std::string dataDirectory, bool withSeven, int width, int height,
              bool vsync)
      : context_(width, height, "Rubik's Cube Solver"),
        threads_(static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))) {
    std::cout << "Renderer: " << context_.describe() << "\n\n"
              << "Controls\n"
              << kControls << "\n\n";

    context_.setVsync(vsync);
    installCallbacks();
    loader_.start(std::move(dataDirectory), withSeven);
  }

  int run() {
    double previous = context_.time();
    while (!context_.shouldClose()) {
      context_.pollEvents();

      const double now = context_.time();
      // Capped so that a stalled frame -- dragging the window, or the driver
      // reclaiming the context -- does not fast-forward the animation.
      const double delta = std::min(now - previous, 0.25);
      previous = now;

      collectResources();
      collectSolverReport();
      controller_.update(delta);
      renderFrame(delta);

      context_.swapBuffers();
    }
    // The service's destructor cancels a running search and joins its threads,
    // so closing the window during an hour-long solve is instant.
    return 0;
  }

 private:
  // --- Wiring -------------------------------------------------------------

  void installCallbacks() {
    GLFWwindow* window = context_.window();
    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
      if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
      static_cast<Application*>(glfwGetWindowUserPointer(w))->onKey(key, mods);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
      if (button != GLFW_MOUSE_BUTTON_LEFT) return;
      static_cast<Application*>(glfwGetWindowUserPointer(w))
          ->onDrag(action == GLFW_PRESS);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
      static_cast<Application*>(glfwGetWindowUserPointer(w))->onCursor(x, y);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
      static_cast<Application*>(glfwGetWindowUserPointer(w))
          ->camera_.zoom(static_cast<float>(dy));
    });
  }

  void collectResources() {
    if (solver_.has_value() || !loader_.done()) return;
    bool korfMissing = false;
    loader_.take(twoPhase_, korf_, korfMissing);
    solver_.emplace(twoPhase_, korf_);
    if (korfMissing) {
      note("pattern databases not found - only Kociemba is available");
      std::cout << "Pattern databases were not found. Generate them with:\n"
                << "  rubiks_solver --generate-pdb\n\n";
    }
  }

  void collectSolverReport() {
    if (!solver_.has_value()) return;
    auto report = solver_->takeReport();
    if (!report.has_value()) return;

    lastReport_ = std::move(report);
    if (lastReport_->verified && !lastReport_->moves.empty()) {
      controller_.loadSolution(lastReport_->moves);
      if (enteredByHand_) {
        // The cube on screen is a stand-in for one in the user's hands. Playing
        // twenty moves at them immediately is useless: they need to read the
        // algorithm and then work through it a move at a time. So show it and
        // wait.
        setSpeedPreset(kFollowSpeedIndex);
        note("solution found: " + std::to_string(lastReport_->length) +
             " moves - press Right for the first move");
      } else {
        setSpeedPreset(kWatchSpeedIndex);
        controller_.play();
        note("solution found: " + std::to_string(lastReport_->length) + " moves");
      }
    } else {
      note(lastReport_->status);
    }
  }

  // --- Input --------------------------------------------------------------

  void onDrag(bool pressed) {
    // In the editor a click paints; only clicks that miss the net fall through
    // to the camera, so the cube can still be turned while editing.
    if (pressed && mode_ == Mode::Editor && paintAtPointer()) return;
    dragging_ = pressed;
    hasCursor_ = false;
  }

  void onCursor(double x, double y) {
    // Tracked always, not just while dragging: the editor needs the pointer
    // position at the moment of the click.
    const double previousX = pointerX_;
    const double previousY = pointerY_;
    pointerX_ = x;
    pointerY_ = y;

    if (!dragging_) return;
    if (hasCursor_) {
      camera_.orbit(static_cast<float>(x - previousX),
                    static_cast<float>(y - previousY));
    }
    hasCursor_ = true;
  }

  /// The pointer in framebuffer pixels, which is what the overlay lays out in.
  /// GLFW reports window coordinates, and on a scaled display the two differ.
  void pointerInFramebuffer(float& x, float& y) const {
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(context_.window(), &windowWidth, &windowHeight);
    int fbWidth = 0;
    int fbHeight = 0;
    context_.framebufferSize(fbWidth, fbHeight);
    const double sx = windowWidth > 0 ? static_cast<double>(fbWidth) / windowWidth : 1.0;
    const double sy = windowHeight > 0 ? static_cast<double>(fbHeight) / windowHeight : 1.0;
    x = static_cast<float>(pointerX_ * sx);
    y = static_cast<float>(pointerY_ * sy);
  }

  /// Handles a click in the editor. Returns true when it hit something.
  bool paintAtPointer() {
    float px = 0.0f;
    float py = 0.0f;
    pointerInFramebuffer(px, py);

    // A palette swatch picks the colour up.
    for (int i = 0; i < kNumFaces; ++i) {
      float x = 0.0f;
      float y = 0.0f;
      float size = 0.0f;
      paletteRect(i, x, y, size);
      if (px >= x && px < x + size && py >= y && py < y + size) {
        editor_.setBrush(static_cast<Face>(i));
        note(std::string("colour: ") + ui::colourName(static_cast<Face>(i)));
        return true;
      }
    }

    // A net cell paints with the colour in hand.
    float originX = 0.0f;
    float originY = 0.0f;
    float cell = 0.0f;
    netRect(originX, originY, cell);
    const int column = static_cast<int>(std::floor((px - originX) / cell));
    const int row = static_cast<int>(std::floor((py - originY) / cell));
    const int facelet = ui::faceletAtCell(column, row);
    if (facelet < 0) return false;

    editor_.setCursor(facelet);
    editor_.paint(facelet, editor_.brush());
    diagnosis_.reset();
    return true;
  }

  void onKey(int key, int mods) {
    if (mode_ == Mode::Editor) {
      onEditorKey(key);
      return;
    }
    switch (key) {
      case GLFW_KEY_ESCAPE:
        if (solver_.has_value() && solver_->running()) {
          solver_->cancel();
          note("cancelling");
        } else {
          context_.requestClose();
        }
        return;
      case GLFW_KEY_H: showHelp_ = !showHelp_; return;
      case GLFW_KEY_E: enterEditor(); return;
      case GLFW_KEY_C:
        // Ctrl+C copies the algorithm; C on its own resets the camera.
        if ((mods & GLFW_MOD_CONTROL) != 0) {
          copySolution();
          return;
        }
        camera_.reset();
        return;
      case GLFW_KEY_TAB: toggleSolver(); return;
      case GLFW_KEY_ENTER:
      case GLFW_KEY_KP_ENTER: startSolve(); return;
      case GLFW_KEY_SPACE: doScramble(); return;
      case GLFW_KEY_BACKSPACE: doReset(); return;
      case GLFW_KEY_P: togglePlayback(); return;
      case GLFW_KEY_N:
      case GLFW_KEY_RIGHT: stepSolution(1); return;
      case GLFW_KEY_LEFT: stepSolution(-1); return;
      case GLFW_KEY_HOME: restartSolution(); return;
      case GLFW_KEY_MINUS:
      case GLFW_KEY_KP_SUBTRACT: setSpeedPreset(speedIndex_ - 1); return;
      case GLFW_KEY_EQUAL:
      case GLFW_KEY_KP_ADD: setSpeedPreset(speedIndex_ + 1); return;
      case GLFW_KEY_LEFT_BRACKET:
        setScrambleLength(scrambleLength_ - 1);
        return;
      case GLFW_KEY_RIGHT_BRACKET:
        setScrambleLength(scrambleLength_ + 1);
        return;
      case GLFW_KEY_1: setThreads(1); return;
      case GLFW_KEY_2: setThreads(2); return;
      case GLFW_KEY_4: setThreads(4); return;
      case GLFW_KEY_8: setThreads(8); return;
      default: break;
    }

    if (key == GLFW_KEY_B && controller_.hasSolution() &&
        controller_.solutionProgress() > 0 && mods == 0) {
      stepSolution(-1);
      return;
    }

    if (const auto face = faceForKey(key)) {
      // Shift turns the other way, Ctrl makes it a half turn. Together they
      // cover all 18 moves from six keys.
      int turn = 0;
      if ((mods & GLFW_MOD_CONTROL) != 0) {
        turn = 1;
      } else if ((mods & GLFW_MOD_SHIFT) != 0) {
        turn = 2;
      }
      requestMove(static_cast<Move>(static_cast<int>(*face) * 3 + turn));
    }
  }

  /// Keys while the net editor is open. Face letters paint rather than turn.
  void onEditorKey(int key) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
      case GLFW_KEY_E:
        mode_ = Mode::Demo;
        note("editor closed");
        return;
      case GLFW_KEY_ENTER:
      case GLFW_KEY_KP_ENTER: applyEditedCube(); return;
      case GLFW_KEY_BACKSPACE:
        editor_.resetToSolved();
        diagnosis_.reset();
        note("editor reset to solved");
        return;
      case GLFW_KEY_DELETE:
        editor_.clear();
        diagnosis_.reset();
        note("stickers cleared");
        return;
      case GLFW_KEY_LEFT:  editor_.moveCursor(-1, 0); return;
      case GLFW_KEY_RIGHT: editor_.moveCursor(1, 0); return;
      case GLFW_KEY_UP:    editor_.moveCursor(0, -1); return;
      case GLFW_KEY_DOWN:  editor_.moveCursor(0, 1); return;
      default: break;
    }

    // Face letters and the digits 1-6 both name a colour. Painting also picks
    // the colour up, so a run of same-coloured stickers is one key then clicks.
    std::optional<Face> colour = faceForKey(key);
    if (!colour) {
      switch (key) {
        case GLFW_KEY_1: colour = Face::U; break;
        case GLFW_KEY_2: colour = Face::R; break;
        case GLFW_KEY_3: colour = Face::F; break;
        case GLFW_KEY_4: colour = Face::D; break;
        case GLFW_KEY_5: colour = Face::L; break;
        case GLFW_KEY_6: colour = Face::B; break;
        default: return;
      }
    }
    editor_.setBrush(*colour);
    editor_.paintAtCursor(*colour);
    diagnosis_.reset();
  }

  /// Puts the solution on the system clipboard in standard notation.
  ///
  /// GLFW owns a clipboard binding already, so this needs no new dependency
  /// and no platform code.
  void copySolution() {
    const std::vector<Move>& moves = controller_.solution();
    if (moves.empty()) {
      note("no solution to copy");
      return;
    }
    glfwSetClipboardString(context_.window(), toString(moves).c_str());
    note("solution copied - " + std::to_string(moves.size()) + " moves");
  }

  void enterEditor() {
    if (cubeIsLocked()) return;
    if (controller_.busy()) {
      note("finish the moves in progress first");
      return;
    }
    // Start from what is on screen, so "edit" means "adjust this" rather than
    // "start again".
    editor_.loadFrom(controller_.cube());
    diagnosis_.reset();
    mode_ = Mode::Editor;
    note("editing - click a colour, then click stickers");
  }

  /// Validate, and only on success hand the cube to the controller.
  void applyEditedCube() {
    diagnosis_ = editor_.validate();
    if (!diagnosis_->valid) {
      // The solver is never called with a state the validator rejected.
      note(diagnosis_->headline);
      return;
    }
    controller_.setCube(diagnosis_->cube);
    controller_.clearSolution();
    lastReport_.reset();
    enteredByHand_ = true;
    mode_ = Mode::Demo;
    note(diagnosis_->cube.isSolved() ? "valid cube - already solved"
                                     : "valid cube - press Enter to solve");
  }

  [[nodiscard]] static std::optional<Face> faceForKey(int key) {
    switch (key) {
      case GLFW_KEY_U: return Face::U;
      case GLFW_KEY_R: return Face::R;
      case GLFW_KEY_F: return Face::F;
      case GLFW_KEY_D: return Face::D;
      case GLFW_KEY_L: return Face::L;
      case GLFW_KEY_B: return Face::B;
      default: return std::nullopt;
    }
  }

  /// Anything that changes the cube is refused while a search is running.
  ///
  /// The search holds its own copy of the position, so letting the cube move
  /// underneath it would produce a solution for a state that is no longer on
  /// screen. Refusing is honest and takes one keystroke to undo.
  [[nodiscard]] bool cubeIsLocked() {
    if (solver_.has_value() && solver_->running()) {
      note("solver is running - press Esc to cancel");
      return true;
    }
    return false;
  }

  void requestMove(Move move) {
    if (cubeIsLocked()) return;
    controller_.enqueue(move);
  }

  void doScramble() {
    if (cubeIsLocked()) return;
    controller_.clearSolution();
    lastReport_.reset();
    const std::uint64_t seed = std::random_device{}();
    // Back to the watching speed: the slow preset exists for following moves on
    // a real cube, and sitting through a 25-move scramble at it is nobody's
    // idea of a demonstration.
    setSpeedPreset(kWatchSpeedIndex);
    controller_.scramble(scrambleLength_, seed);
    enteredByHand_ = false;
    note("scrambled");
  }

  void doReset() {
    if (cubeIsLocked()) return;
    controller_.reset();
    lastReport_.reset();
    enteredByHand_ = false;
    note("reset");
  }

  /// One move forward or back through the solution.
  void stepSolution(int direction) {
    if (!controller_.hasSolution()) {
      note("no solution loaded");
      return;
    }
    if (controller_.busy()) return;  // the controller ignores it anyway
    if (direction > 0) {
      if (controller_.solutionComplete()) {
        note("solution finished - Home to start again");
        return;
      }
      controller_.stepForward();
    } else {
      if (controller_.solutionProgress() == 0) {
        note("already at the first move");
        return;
      }
      controller_.stepBackward();
    }
  }

  void restartSolution() {
    if (!controller_.hasSolution()) return;
    controller_.restartSolution();
    note("back to the cube you entered");
  }

  void setSpeedPreset(int index) {
    speedIndex_ = std::clamp(index, 0, static_cast<int>(kSpeedPresets.size()) - 1);
    controller_.setSpeed(
        kSpeedPresets[static_cast<std::size_t>(speedIndex_)].movesPerSecond);
    note(std::string("speed ") +
         kSpeedPresets[static_cast<std::size_t>(speedIndex_)].label);
  }

  void togglePlayback() {
    if (controller_.playing()) {
      controller_.pause();
    } else {
      controller_.play();
    }
  }

  void toggleSolver() {
    choice_ = choice_ == ui::SolverChoice::Kociemba ? ui::SolverChoice::Korf
                                                    : ui::SolverChoice::Kociemba;
    if (choice_ == ui::SolverChoice::Korf && korf_ == nullptr) {
      note("optimal solver unavailable - pattern databases missing");
    }
  }

  /// The optimal solver can only finish shallow positions in a sitting, so the
  /// scramble length is adjustable. A 25-move scramble is a random position,
  /// which Korf cannot solve in the time anyone will wait; eight or nine moves
  /// is a position it solves while you watch.
  void setScrambleLength(int length) {
    scrambleLength_ = std::clamp(length, kMinScramble, kMaxScramble);
    note("scramble length " + std::to_string(scrambleLength_));
  }

  void setThreads(int count) {
    threads_ = count;
    note(std::to_string(count) + " solver thread" + (count == 1 ? "" : "s"));
  }

  void startSolve() {
    if (!solver_.has_value()) {
      note("still loading");
      return;
    }
    if (solver_->running()) {
      note("already solving - press Esc to cancel");
      return;
    }
    if (choice_ == ui::SolverChoice::Korf && !solver_->korfAvailable()) {
      note("optimal solver unavailable - pattern databases missing");
      return;
    }
    if (controller_.cube().isSolved()) {
      note("already solved");
      return;
    }
    if (controller_.busy()) {
      // Solving the position that is mid-animation would solve the wrong cube.
      note("finish the moves in progress first");
      return;
    }

    controller_.clearSolution();
    lastReport_.reset();
    solver_->start(choice_, controller_.cube(), threads_,
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       kKorfTimeLimit));
  }

  void note(std::string text) {
    message_ = std::move(text);
    messageExpiry_ = context_.time() + 4.0;
  }

  // --- Editor geometry ----------------------------------------------------
  //
  // One place computes the net's position and cell size; the renderer and the
  // hit-test both call it, so a click cannot land on a different sticker than
  // the one under the pointer.

  /// Lines of text reserved between the palette and the net: the verdict
  /// headline, its detail and its explanation, wrapped.
  static constexpr float kVerdictLines = 7.0f;

  /// The whole editor block, solved once so every part agrees.
  struct EditorLayout {
    float x = 0.0f;        ///< left edge of the block
    float titleY = 0.0f;   ///< "ENTER YOUR CUBE"
    float paletteY = 0.0f; ///< top of the colour swatches
    float verdictY = 0.0f; ///< first line of the validation result
    float netY = 0.0f;     ///< top of the 3x3 grid block
    float cell = 0.0f;     ///< side of one sticker
    float swatch = 0.0f;   ///< side of one palette swatch
    float line = 0.0f;     ///< text line height
    float scale = 1.0f;
  };

  [[nodiscard]] EditorLayout editorLayout() const {
    int width = 0;
    int height = 0;
    context_.framebufferSize(width, height);

    EditorLayout out;
    out.scale = std::max(1.0f, static_cast<float>(height) / 420.0f);
    out.line = rr::TextOverlay::lineHeight(out.scale);
    out.x = std::max(12.0f, static_cast<float>(width) * 0.02f);

    // Anchor the grid to the bottom of the window and lay the text upward from
    // it. Every earlier attempt stacked offsets downward from a guessed top and
    // drifted; measuring back from a fixed edge cannot.
    const float bottom = static_cast<float>(height) * 0.97f;
    // Below the status panel as it was actually drawn, not a guessed fraction:
    // the panel grows by several lines once a solve has been reported, and a
    // fixed fraction put the editor straight through the middle of it.
    const float minTop = std::max(static_cast<float>(height) * 0.20f,
                                  statusPanelBottom_ + out.line * 2.0f);
    const float usableW = static_cast<float>(width) * 0.46f;

    out.cell = std::floor(usableW / ui::kNetColumns);
    for (;;) {
      out.swatch = out.cell * 1.1f;
      out.netY = bottom - out.cell * ui::kNetRows;
      out.verdictY = out.netY - out.line * kVerdictLines;
      // 1.4 lines of gap, plus one for the count labels under the swatches.
      out.paletteY = out.verdictY - out.line * 2.4f - out.swatch;
      out.titleY = out.paletteY - out.line * 1.8f;
      if (out.titleY >= minTop || out.cell <= 8.0f) break;
      out.cell -= 1.0f;
    }
    return out;
  }

  void netRect(float& originX, float& originY, float& cell) const {
    const EditorLayout layout = editorLayout();
    originX = layout.x;
    originY = layout.netY;
    cell = layout.cell;
  }

  void paletteRect(int index, float& x, float& y, float& size) const {
    const EditorLayout layout = editorLayout();
    size = layout.swatch;
    x = layout.x + static_cast<float>(index) * (size + layout.cell * 0.22f);
    y = layout.paletteY;
  }

  void drawEditor(int width, float scale) {
    const EditorLayout layout = editorLayout();
    const float line = layout.line;
    const float cell = layout.cell;
    const float originX = layout.x;
    const float originY = layout.netY;

    const glm::vec3 body{0.88f, 0.90f, 0.94f};
    const glm::vec3 dim{0.58f, 0.62f, 0.70f};

    // Backing panel behind the whole editor.
    overlay_.panel(originX - cell * 0.6f, layout.titleY - line * 0.8f,
                   cell * (ui::kNetColumns + 1.2f),
                   (originY + cell * ui::kNetRows) - layout.titleY + line * 1.6f,
                   kPanelColour);
    overlay_.text(originX, layout.titleY, scale, glm::vec3(0.65f, 0.78f, 1.0f),
                  "ENTER YOUR CUBE");

    // The stickers.
    for (int facelet = 0; facelet < kNumFacelets; ++facelet) {
      int column = 0;
      int row = 0;
      if (!ui::netCell(facelet, column, row)) continue;
      const float x = originX + static_cast<float>(column) * cell;
      const float y = originY + static_cast<float>(row) * cell;

      const bool selected = facelet == editor_.cursor();
      // A light box behind each sticker gives the grid its lines, and doubles
      // as the cursor highlight.
      overlay_.panel(x, y, cell, cell,
                     selected ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
                              : glm::vec4(0.16f, 0.17f, 0.20f, 1.0f));

      const auto colour = editor_.colourAt(facelet);
      const float inset = cell * 0.10f;
      if (colour.has_value()) {
        const glm::vec3 rgb = kStickerColours[static_cast<std::size_t>(*colour)];
        overlay_.panel(x + inset, y + inset, cell - inset * 2.0f,
                       cell - inset * 2.0f, glm::vec4(rgb, 1.0f));
      } else {
        // Unset stickers read as empty holes rather than as a colour.
        overlay_.panel(x + inset, y + inset, cell - inset * 2.0f,
                       cell - inset * 2.0f, glm::vec4(0.07f, 0.08f, 0.10f, 1.0f));
      }
      if (ui::CubeEditor::isCentre(facelet)) {
        // Centres are fixed; mark them so nobody wonders why they will not paint.
        overlay_.text(x + cell * 0.35f, y + cell * 0.28f, scale * 0.8f,
                      glm::vec3(0.0f, 0.0f, 0.0f), ".");
      }
    }

    // The palette, above the net so the colour in hand is the first thing seen.
    const std::array<int, kNumFaces> counts = editor_.colourCounts();
    for (int i = 0; i < kNumFaces; ++i) {
      float x = 0.0f;
      float y = 0.0f;
      float size = 0.0f;
      paletteRect(i, x, y, size);
      const bool held = static_cast<Face>(i) == editor_.brush();
      overlay_.panel(x - 2.0f, y - 2.0f, size + 4.0f, size + 4.0f,
                     held ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
                          : glm::vec4(0.16f, 0.17f, 0.20f, 1.0f));
      overlay_.panel(x, y, size, size,
                     glm::vec4(kStickerColours[static_cast<std::size_t>(i)], 1.0f));
      // How many of this colour are placed: 9 is what a real cube has.
      const std::string count = std::to_string(counts[static_cast<std::size_t>(i)]);
      overlay_.text(x + size * 0.5f -
                        rr::TextOverlay::widthOf(count, scale) * 0.5f,
                    y + size + 3.0f, scale,
                    counts[static_cast<std::size_t>(i)] == 9 ? dim
                                                             : glm::vec3(0.95f, 0.75f, 0.35f),
                    count);
    }

    // The verdict, between the palette and the net.
    float y = layout.verdictY;
    const int missing = editor_.unsetCount();
    if (missing > 0) {
      overlay_.text(originX, y, scale, glm::vec3(0.95f, 0.75f, 0.35f),
                    std::to_string(missing) + " STICKERS NOT SET");
    } else if (diagnosis_.has_value()) {
      const glm::vec3 tone = diagnosis_->valid ? glm::vec3(0.55f, 0.85f, 0.55f)
                                               : glm::vec3(0.95f, 0.45f, 0.40f);
      overlay_.text(originX, y, scale, tone,
                    diagnosis_->valid ? "VALID CUBE" : "INVALID CUBE");
      y += line;
      overlay_.text(originX, y, scale, tone, diagnosis_->headline);
      y += line;
      // Wrap the detail and the explanation so a long sentence stays on screen.
      const auto maxChars = static_cast<std::size_t>(
          (cell * ui::kNetColumns) / rr::TextOverlay::advance(scale));
      // Bounded by the space reserved for it, so a long explanation is clipped
      // rather than allowed to run over the net.
      const float limit = layout.verdictY + line * kVerdictLines;
      for (const std::string& text : {diagnosis_->detail, diagnosis_->explanation}) {
        for (const std::string& row : wrap(text, maxChars)) {
          if (y >= limit) break;
          overlay_.text(originX, y, scale, dim, row);
          y += line;
        }
      }
    } else {
      overlay_.text(originX, y, scale, body, "PRESS ENTER TO VALIDATE");
    }

    // Controls, on the right, with the same backing the demo panel uses.
    if (showHelp_) {
      const float pad = 10.0f * scale;
      const float helpWidth = rr::TextOverlay::widthOf(kEditorControls, scale);
      int helpLines = 1;
      for (const char* c = kEditorControls; *c != '\0'; ++c) {
        if (*c == '\n') ++helpLines;
      }
      const float x = static_cast<float>(width) - helpWidth - pad * 2.5f;
      overlay_.panel(x - pad, pad * 0.5f, helpWidth + pad * 2.0f,
                     line * static_cast<float>(helpLines) + pad, kPanelColour);
      overlay_.text(x, pad, scale, dim, kEditorControls);
    }
  }

  /// The move the user should be performing right now, spelled out, followed by
  /// the whole algorithm with that move picked out.
  ///
  /// The notation is the instruction; the words underneath are a gloss for
  /// somebody who does not read notation fluently, which is most people holding
  /// a cube for the first time.
  void drawSolutionPanel(float top, float scale) {
    const std::vector<Move>& solution = controller_.solution();
    const int done = controller_.solutionProgress();
    const int total = static_cast<int>(solution.size());

    const glm::vec3 dim{0.58f, 0.62f, 0.70f};
    const glm::vec3 body{0.88f, 0.90f, 0.94f};
    const glm::vec3 good{0.55f, 0.85f, 0.55f};
    const glm::vec3 accent{0.98f, 0.82f, 0.35f};

    const float line = rr::TextOverlay::lineHeight(scale);
    const float pad = 10.0f * scale;
    const float x = pad * 1.5f;

    // The move to perform is the one being animated if a step is running,
    // otherwise the next one waiting.
    const std::optional<Move> running = controller_.animatingSolutionMove();
    const std::optional<Move> next = controller_.nextSolutionMove();
    const std::optional<Move> current = running.has_value() ? running : next;

    // Which token to pick out in the algorithm: the one in flight, else the
    // next one due.
    const int highlight = running.has_value() ? done : (next.has_value() ? done : -1);

    const float bigScale = scale * 2.2f;
    const float bigLine = rr::TextOverlay::lineHeight(bigScale);

    const std::string counter =
        done >= total ? std::string("SOLVED - ") + std::to_string(total) + " MOVES DONE"
                      : "MOVE " + std::to_string(done + 1) + " OF " +
                            std::to_string(total);

    const auto algorithmLines = layoutAlgorithm(solution, 34);
    const float height = line * 1.4f + bigLine + line * 2.4f +
                         line * static_cast<float>(algorithmLines.size()) +
                         line * 1.6f;
    overlay_.panel(pad * 0.5f, top - line * 0.5f,
                   rr::TextOverlay::advance(scale) * 38.0f + pad * 2.0f, height,
                   kPanelColour);

    float y = top;
    overlay_.text(x, y, scale, current.has_value() ? accent : good, counter);
    y += line * 1.3f;

    if (current.has_value()) {
      // The notation, large. This is the authoritative instruction.
      overlay_.text(x, y, bigScale, accent, std::string(toString(*current)));
      y += bigLine;
      const ui::MoveDescription d = ui::describeMove(*current);
      overlay_.text(x, y, scale, body, std::string(d.face) + " FACE");
      y += line;
      overlay_.text(x, y, scale, body, d.turn);
      y += line * 1.6f;
    } else {
      y += bigLine + line * 2.6f;
    }

    overlay_.text(x, y, scale, dim, "ALGORITHM  (CTRL+C TO COPY)");
    y += line * 1.2f;
    drawAlgorithm(x, y, scale, algorithmLines, highlight, dim, accent, good);
  }

  /// The algorithm split into display lines, each a list of (token, index).
  using AlgorithmLine = std::vector<std::pair<std::string, int>>;

  [[nodiscard]] static std::vector<AlgorithmLine> layoutAlgorithm(
      const std::vector<Move>& moves, std::size_t width) {
    std::vector<AlgorithmLine> lines;
    AlgorithmLine current;
    std::size_t used = 0;
    for (std::size_t i = 0; i < moves.size(); ++i) {
      // Every token is drawn bracketed when it is the current one, so reserve
      // the brackets in the width to stop the line reflowing as it advances.
      const std::string token = std::string(toString(moves[i]));
      const std::size_t needed = token.size() + 2 + (current.empty() ? 0 : 1);
      if (!current.empty() && used + needed > width) {
        lines.push_back(current);
        current.clear();
        used = 0;
      }
      used += current.empty() ? token.size() + 2 : needed;
      current.emplace_back(token, static_cast<int>(i));
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
  }

  /// Draws the algorithm token by token so the current move can be picked out.
  /// The font is monospaced, so a token's x position is just its character
  /// offset times the advance.
  void drawAlgorithm(float x, float y, float scale,
                     const std::vector<AlgorithmLine>& lines, int highlight,
                     const glm::vec3& doneColour, const glm::vec3& currentColour,
                     const glm::vec3& pendingColour) {
    const float advance = rr::TextOverlay::advance(scale);
    const float line = rr::TextOverlay::lineHeight(scale);
    const int progress = controller_.solutionProgress();

    for (const AlgorithmLine& row : lines) {
      float cursor = x;
      for (const auto& entry : row) {
        const std::string& token = entry.first;
        const int index = entry.second;
        const bool isCurrent = index == highlight;
        // Brackets mark the current move, so it reads correctly even in a
        // screenshot or for anyone who cannot pick the colour out.
        const std::string text = isCurrent ? "[" + token + "]" : " " + token + " ";
        const glm::vec3 colour = isCurrent ? currentColour
                                           : (index < progress ? doneColour
                                                               : pendingColour);
        overlay_.text(cursor, y, scale, colour, text);
        cursor += advance * static_cast<float>(text.size());
      }
      y += line;
    }
  }

  /// Greedy word wrap, so an explanation fits the panel it is drawn in.
  [[nodiscard]] static std::vector<std::string> wrap(const std::string& text,
                                                     std::size_t width) {
    std::vector<std::string> lines;
    if (width < 8) width = 8;
    std::string current;
    std::string word;
    const auto flushWord = [&] {
      if (word.empty()) return;
      if (current.empty()) {
        current = word;
      } else if (current.size() + 1 + word.size() <= width) {
        current += " " + word;
      } else {
        lines.push_back(current);
        current = word;
      }
      word.clear();
    };
    for (const char c : text) {
      if (c == ' ') {
        flushWord();
      } else {
        word.push_back(c);
      }
    }
    flushWord();
    if (!current.empty()) lines.push_back(current);
    return lines;
  }

  // --- Drawing ------------------------------------------------------------

  void renderFrame(double delta) {
    int width = 0;
    int height = 0;
    context_.framebufferSize(width, height);
    if (width <= 0 || height <= 0) return;

    renderer_.beginFrame(width, height);

    const ui::CubeController::Animation animation = controller_.animation();
    rr::LayerRotation rotation;
    rotation.active = animation.active;
    rotation.face = face(animation.move);
    rotation.angleRadians = animation.angleRadians;

    renderer_.draw(controller_.cube(), rotation, camera_,
                   static_cast<float>(width) / static_cast<float>(height));

    // A little smoothing, or the numbers are unreadable.
    frameMs_ = frameMs_ * 0.9 + delta * 1000.0 * 0.1;
    drawHud(width, height);
  }

  void drawHud(int width, int height) {
    const float scale = std::max(1.0f, static_cast<float>(height) / 420.0f);
    const float line = rr::TextOverlay::lineHeight(scale);
    const float pad = 10.0f * scale;

    const glm::vec3 heading{0.65f, 0.78f, 1.0f};
    const glm::vec3 body{0.88f, 0.90f, 0.94f};
    const glm::vec3 dim{0.58f, 0.62f, 0.70f};
    const glm::vec3 good{0.55f, 0.85f, 0.55f};
    const glm::vec3 warn{0.95f, 0.75f, 0.35f};

    overlay_.begin(width, height);

    // --- Status, top left ---
    std::vector<std::pair<std::string, glm::vec3>> lines;
    lines.emplace_back("RUBIK'S CUBE SOLVER", heading);
    lines.emplace_back(
        std::string("CUBE     ") +
            (controller_.cube().isSolved()
                 ? "SOLVED"
                 : (enteredByHand_ ? "YOURS (ENTERED BY HAND)" : "SCRAMBLED")),
        controller_.cube().isSolved() ? good : body);
    lines.emplace_back("SOLVER   " + std::string(toString(choice_)) +
                           (choice_ == ui::SolverChoice::Korf ? "" : " (TAB)"),
                       body);
    if (choice_ == ui::SolverChoice::Korf) {
      lines.emplace_back("THREADS  " + std::to_string(threads_), body);
    }
    if (!controller_.hasSolution() || mode_ == Mode::Editor) {
      lines.emplace_back(
          "SPEED    " +
              std::string(kSpeedPresets[static_cast<std::size_t>(speedIndex_)].label),
          dim);
    }
    lines.emplace_back("SCRAMBLE " + std::to_string(scrambleLength_) + " MOVES",
                       dim);
    lines.emplace_back("FRAME    " + formatDouble(frameMs_, 1) + " MS", dim);

    if (!loader_.done()) {
      lines.emplace_back(loader_.status() + "...", warn);
    } else if (solver_.has_value() && solver_->running()) {
      lines.emplace_back(solver_->statusLine() + "...", warn);
    }

    if (controller_.hasSolution() && mode_ != Mode::Editor) {
      const int done = controller_.solutionProgress();
      const int total = static_cast<int>(controller_.solution().size());
      lines.emplace_back("MOVE     " + std::to_string(done) + " / " +
                             std::to_string(total) +
                             (controller_.playing() ? "   PLAYING" : "   PAUSED"),
                         body);
      lines.emplace_back(
          "SPEED    " +
              std::string(kSpeedPresets[static_cast<std::size_t>(speedIndex_)].label),
          dim);
    }

    if (lastReport_.has_value() && mode_ != Mode::Editor) {
      const ui::SolveReport& r = *lastReport_;
      lines.emplace_back("", body);
      lines.emplace_back("LAST SOLVE", heading);
      lines.emplace_back("  RESULT   " + r.status,
                         r.verified ? good : warn);
      if (r.solved) {
        lines.emplace_back("  LENGTH   " + std::to_string(r.length) + " MOVES",
                           body);
        lines.emplace_back("  TIME     " + formatDouble(r.seconds, 3) + " S", body);
        lines.emplace_back("  NODES    " + formatCount(r.nodesExpanded), body);
        if (r.initialHeuristic >= 0) {
          lines.emplace_back("  START H  " + std::to_string(r.initialHeuristic),
                             body);
        }
        lines.emplace_back(std::string("  VERIFIED ") + (r.verified ? "YES" : "NO"),
                           r.verified ? good : warn);
      }

    }

    float widest = 0.0f;
    for (const auto& entry : lines) {
      widest = std::max(widest, rr::TextOverlay::widthOf(entry.first, scale));
    }
    overlay_.panel(pad * 0.5f, pad * 0.5f, widest + pad * 2.0f,
                   line * static_cast<float>(lines.size()) + pad,
                   kPanelColour);

    float y = pad;
    for (const auto& entry : lines) {
      overlay_.text(pad * 1.5f, y, scale, entry.second, entry.first);
      y += line;
    }
    const float panelBottom = y;
    // The editor and the solution block both need to start clear of this.
    statusPanelBottom_ = panelBottom;

    // --- Controls, top right ---
    if (showHelp_ && mode_ == Mode::Demo) {
      const float helpWidth = rr::TextOverlay::widthOf(kControls, scale);
      int helpLines = 1;
      for (const char* p = kControls; *p != '\0'; ++p) {
        if (*p == '\n') ++helpLines;
      }
      const float x = static_cast<float>(width) - helpWidth - pad * 2.5f;
      overlay_.panel(x - pad, pad * 0.5f, helpWidth + pad * 2.0f,
                     line * static_cast<float>(helpLines) + pad, kPanelColour);
      overlay_.text(x, pad, scale, dim, kControls);
    }

    if (mode_ == Mode::Editor) {
      drawEditor(width, scale);
    } else if (controller_.hasSolution()) {
      drawSolutionPanel(panelBottom + line * 2.0f, scale);
    }

    // --- Transient message, just under the status panel ---
    if (!message_.empty() && context_.time() < messageExpiry_) {
      overlay_.text(pad * 1.5f, panelBottom + line * 0.5f, scale, warn, message_);
    }

    overlay_.end();
  }

  // Declared before anything that uses the context, and destroyed after it.
  rr::GlContext context_;
  rr::CubeRenderer renderer_;
  rr::TextOverlay overlay_;
  rr::Camera camera_;
  ui::CubeController controller_;

  ResourceLoader loader_;
  // twoPhase_ owns the move tables the heuristic refers to, so it must outlive
  // korf_: declared first, destroyed last.
  std::shared_ptr<const TwoPhaseTables> twoPhase_;
  std::shared_ptr<const korf::KorfHeuristic> korf_;
  std::optional<ui::SolverService> solver_;

  Mode mode_ = Mode::Demo;
  ui::CubeEditor editor_;
  /// The last validation verdict, kept so the editor can display it.
  std::optional<CubeDiagnosis> diagnosis_;
  /// True once a cube came from the editor rather than from a scramble.
  bool enteredByHand_ = false;

  ui::SolverChoice choice_ = ui::SolverChoice::Kociemba;
  int threads_ = 1;
  int scrambleLength_ = kDefaultScramble;
  int speedIndex_ = kWatchSpeedIndex;
  /// Where the status panel finished last frame, so the blocks underneath it
  /// can be placed without overlapping. Written during drawing, read by the
  /// layout -- including from the mouse handler between frames.
  float statusPanelBottom_ = 0.0f;
  std::optional<ui::SolveReport> lastReport_;

  std::string message_;
  double messageExpiry_ = 0.0;
  double frameMs_ = 0.0;
  bool showHelp_ = true;
  bool dragging_ = false;
  bool hasCursor_ = false;
  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
};

}  // namespace

int main(int argc, char** argv) {
  std::string dataDirectory = "data";
  bool withSeven = true;
  bool vsync = true;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--data-dir" && i + 1 < argc) {
      dataDirectory = argv[++i];
    } else if (arg == "--no-seven") {
      withSeven = false;
    } else if (arg == "--no-vsync") {
      vsync = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "rubiks_gui [--data-dir <path>] [--no-seven] [--no-vsync]\n\n"
                << "Controls\n"
                << kControls << "\n";
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 2;
    }
  }

  try {
    int width = 0;
    int height = 0;
    chooseWindowSize(width, height);
    Application app(std::move(dataDirectory), withSeven, width, height, vsync);
    return app.run();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
