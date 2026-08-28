#include "render/CubeRenderer.h"

#include "ui/CubeLayout.h"

#include <glm/gtc/matrix_transform.hpp>

namespace rubik::render {
namespace {

/// Distance between the centres of neighbouring cubies. One unit, so the grid
/// coordinates from CubeLayout are the positions.
constexpr float kSpacing = 1.0f;

const char* const kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aFace;
layout(location = 3) in float aSticker;

uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProjection;
uniform vec3 uColours[6];
uniform vec3 uBodyColour;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec3 vColour;

void main() {
  vec4 world = uModel * vec4(aPosition, 1.0);
  vWorldPosition = world.xyz;
  vNormal = normalize(uNormalMatrix * aNormal);

  // Every vertex of a quad carries the same face and sticker flag, so this
  // interpolates to a constant across the triangle. Choosing the colour here
  // rather than in the fragment shader keeps the array indexing off the
  // per-pixel path.
  vec3 sticker = uColours[int(aFace)];
  vColour = mix(uBodyColour, sticker, aSticker);

  gl_Position = uViewProjection * world;
}
)";

const char* const kFragmentShader = R"(#version 330 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vColour;

uniform vec3 uEye;
uniform vec3 uKeyLight;
uniform vec3 uFillLight;

out vec4 fragColour;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 view = normalize(uEye - vWorldPosition);

  // A key light over the viewer's shoulder and a dimmer fill from the opposite
  // side, so a face turned away from the key light is shaded rather than black
  // and the cube still reads as three distinct planes.
  float key = max(dot(normal, normalize(uKeyLight)), 0.0);
  float fill = max(dot(normal, normalize(uFillLight)), 0.0);

  vec3 halfway = normalize(normalize(uKeyLight) + view);
  float specular = pow(max(dot(normal, halfway), 0.0), 48.0) * 0.25;

  vec3 lit = vColour * (0.40 + 0.68 * key + 0.16 * fill) + vec3(specular);
  fragColour = vec4(lit, 1.0);
}
)";

/// The six face colours, in the Face order U, R, F, D, L, B: the usual
/// white, red, green, yellow, orange, blue.
constexpr std::array<glm::vec3, 6> kFaceColours{{
    {0.94f, 0.94f, 0.94f},  // U -- white
    {0.78f, 0.13f, 0.16f},  // R -- red
    {0.10f, 0.56f, 0.28f},  // F -- green
    {0.96f, 0.80f, 0.11f},  // D -- yellow
    {0.90f, 0.42f, 0.09f},  // L -- orange
    {0.09f, 0.32f, 0.68f},  // B -- blue
}};

constexpr glm::vec3 kBodyColour{0.06f, 0.06f, 0.07f};

glm::vec3 axisOf(Face face) {
  const ui::Vec3i normal = ui::faceNormal(face);
  return glm::vec3(static_cast<float>(normal.x), static_cast<float>(normal.y),
                   static_cast<float>(normal.z));
}

}  // namespace

CubeRenderer::CubeRenderer()
    : shader_(kVertexShader, kFragmentShader), mesh_(buildCubieMesh()) {}

void CubeRenderer::beginFrame(int framebufferWidth, int framebufferHeight) {
  glViewport(0, 0, framebufferWidth, framebufferHeight);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glDisable(GL_BLEND);

  glClearColor(0.09f, 0.10f, 0.13f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void CubeRenderer::gatherColours(const FaceletArray& facelets, int cubieIndex) {
  const ui::Vec3i position =
      ui::cubiePositions()[static_cast<std::size_t>(cubieIndex)];
  for (int f = 0; f < kNumFaces; ++f) {
    const Face side = static_cast<Face>(f);
    const int index = ui::faceletIndex(position, side);
    colours_[static_cast<std::size_t>(f)] =
        index < 0 ? kBodyColour
                  : kFaceColours[static_cast<std::size_t>(
                        facelets[static_cast<std::size_t>(index)])];
  }
}

void CubeRenderer::draw(const Cube& cube, const LayerRotation& rotation,
                        const Camera& camera, float aspectRatio) {
  facelets_ = toFacelets(cube);

  const glm::mat4 viewProjection =
      camera.projectionMatrix(aspectRatio) * camera.viewMatrix();
  const glm::vec3 eye = camera.eyePosition();

  shader_.use();
  shader_.set("uViewProjection", viewProjection);
  shader_.set("uEye", eye);
  // The key light follows the camera, so the face being looked at is always
  // the best lit one however the cube is orbited.
  shader_.set("uKeyLight", glm::normalize(eye + glm::vec3(1.5f, 3.0f, 1.0f)));
  shader_.set("uFillLight", glm::vec3(-1.0f, -0.6f, -1.2f));
  shader_.set("uBodyColour", kBodyColour);

  const glm::mat4 layerRotation =
      rotation.active
          ? glm::rotate(glm::mat4(1.0f), rotation.angleRadians,
                        axisOf(rotation.face))
          : glm::mat4(1.0f);

  for (int i = 0; i < ui::kNumCubies; ++i) {
    const ui::Vec3i grid = ui::cubiePositions()[static_cast<std::size_t>(i)];

    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(static_cast<float>(grid.x), static_cast<float>(grid.y),
                  static_cast<float>(grid.z)) *
            kSpacing);

    // The nine cubies of the turning face are spun about the cube's centre;
    // the other eighteen are drawn where they are. This is the whole of the
    // animation as far as the renderer is concerned.
    if (rotation.active && ui::inLayer(grid, rotation.face)) {
      model = layerRotation * model;
    }

    shader_.set("uModel", model);
    // The model matrix is a rotation and a translation only, so its upper 3x3
    // is already orthonormal and can be used for normals directly -- no
    // inverse-transpose needed, and none computed 27 times a frame.
    shader_.set("uNormalMatrix", glm::mat3(model));

    gatherColours(facelets_, i);
    shader_.setArray("uColours", colours_.data(), 6);

    mesh_.draw();
  }
}

}  // namespace rubik::render
