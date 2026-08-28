#include "core/Facelets.h"

#include "core/Error.h"

#include <cctype>
#include <string>

namespace rubik {

// Facelet indices, following the layout documented in Facelets.h.
// Each corner triple is listed in the order the corner is named, so the U or D
// sticker always comes first: URF -> {U9, R1, F3}.
const std::array<std::array<std::uint8_t, 3>, kNumCorners> kCornerFacelets = {{
    {{8, 9, 20}},    // URF
    {{6, 18, 38}},   // UFL
    {{0, 36, 47}},   // ULB
    {{2, 45, 11}},   // UBR
    {{29, 26, 15}},  // DFR
    {{27, 44, 24}},  // DLF
    {{33, 53, 42}},  // DBL
    {{35, 17, 51}},  // DRB
}};

const std::array<std::array<std::uint8_t, 2>, kNumEdges> kEdgeFacelets = {{
    {{5, 10}},   // UR
    {{7, 19}},   // UF
    {{3, 37}},   // UL
    {{1, 46}},   // UB
    {{32, 16}},  // DR
    {{28, 25}},  // DF
    {{30, 43}},  // DL
    {{34, 52}},  // DB
    {{23, 12}},  // FR
    {{21, 41}},  // FL
    {{50, 39}},  // BL
    {{48, 14}},  // BR
}};

namespace {

constexpr std::string_view kFaceLetters = "URFDLB";
// Standard Western colour scheme: white up, green front, red right.
constexpr std::string_view kColourLetters = "WRGYOB";

/// The colour a facelet shows on a solved cube, which is simply the face it
/// belongs to.
constexpr Face solvedColour(std::uint8_t facelet) noexcept {
  return static_cast<Face>(facelet / 9);
}

/// True when a colour belongs to the U or D face. Corner orientation is defined
/// by which of the three stickers carries the U/D colour.
constexpr bool isUpDown(Face f) noexcept {
  return f == Face::U || f == Face::D;
}

}  // namespace

char faceLetter(Face f) noexcept {
  return kFaceLetters[static_cast<std::uint8_t>(f)];
}

FaceletArray toFacelets(const Cube& cube) {
  FaceletArray out{};

  // Centres never move, so they identify the faces.
  for (int f = 0; f < kNumFaces; ++f) {
    out[static_cast<std::size_t>(f) * 9 + 4] = static_cast<Face>(f);
  }

  // A corner cubie j sitting in slot i with twist `ori` presents its k-th
  // sticker at the slot's (k + ori)-th facelet position.
  for (int slot = 0; slot < kNumCorners; ++slot) {
    const std::uint8_t cubie = cube.cornerPerm()[static_cast<std::size_t>(slot)];
    const std::uint8_t ori = cube.cornerOri()[static_cast<std::size_t>(slot)];
    for (int k = 0; k < 3; ++k) {
      const std::uint8_t dest =
          kCornerFacelets[static_cast<std::size_t>(slot)][static_cast<std::size_t>((k + ori) % 3)];
      out[dest] = solvedColour(kCornerFacelets[cubie][static_cast<std::size_t>(k)]);
    }
  }

  for (int slot = 0; slot < kNumEdges; ++slot) {
    const std::uint8_t cubie = cube.edgePerm()[static_cast<std::size_t>(slot)];
    const std::uint8_t ori = cube.edgeOri()[static_cast<std::size_t>(slot)];
    for (int k = 0; k < 2; ++k) {
      const std::uint8_t dest =
          kEdgeFacelets[static_cast<std::size_t>(slot)][static_cast<std::size_t>((k + ori) % 2)];
      out[dest] = solvedColour(kEdgeFacelets[cubie][static_cast<std::size_t>(k)]);
    }
  }

  return out;
}

Cube fromFacelets(const FaceletArray& facelets) {
  // Nine stickers of every colour, or the sticker set itself is wrong.
  std::array<int, kNumFaces> counts{};
  for (const Face f : facelets) {
    counts[static_cast<std::size_t>(f)]++;
  }
  for (int f = 0; f < kNumFaces; ++f) {
    if (counts[static_cast<std::size_t>(f)] != 9) {
      throw InvalidStateError(
          std::string("colour ") + faceLetter(static_cast<Face>(f)) +
          " appears " + std::to_string(counts[static_cast<std::size_t>(f)]) +
          " times, expected 9",
          CubeFault::ColourCount);
    }
  }

  // Centres must be the six distinct faces in the canonical order; otherwise
  // the cube is described in a rotated frame we cannot interpret.
  for (int f = 0; f < kNumFaces; ++f) {
    const Face centre = facelets[static_cast<std::size_t>(f) * 9 + 4];
    if (centre != static_cast<Face>(f)) {
      throw InvalidStateError(
          std::string("centre of face ") + faceLetter(static_cast<Face>(f)) +
          " shows colour " + faceLetter(centre) +
          "; the cube must be given with U up and F front",
          CubeFault::Centre);
    }
  }

  // Build the reference colour triples/pairs once: the colours corner cubie j
  // and edge cubie j show when solved.
  std::array<std::array<Face, 3>, kNumCorners> cornerColours{};
  for (int c = 0; c < kNumCorners; ++c) {
    for (int k = 0; k < 3; ++k) {
      cornerColours[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)] =
          solvedColour(kCornerFacelets[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)]);
    }
  }
  std::array<std::array<Face, 2>, kNumEdges> edgeColours{};
  for (int e = 0; e < kNumEdges; ++e) {
    for (int k = 0; k < 2; ++k) {
      edgeColours[static_cast<std::size_t>(e)][static_cast<std::size_t>(k)] =
          solvedColour(kEdgeFacelets[static_cast<std::size_t>(e)][static_cast<std::size_t>(k)]);
    }
  }

  std::array<std::uint8_t, kNumCorners> cp{};
  std::array<std::uint8_t, kNumCorners> co{};
  std::array<std::uint8_t, kNumEdges> ep{};
  std::array<std::uint8_t, kNumEdges> eo{};

  for (int slot = 0; slot < kNumCorners; ++slot) {
    // Orientation is the position of the U/D-coloured sticker within the slot's
    // facelet triple. Exactly one of the three must carry a U or D colour.
    int ori = -1;
    for (int k = 0; k < 3; ++k) {
      const Face colour =
          facelets[kCornerFacelets[static_cast<std::size_t>(slot)][static_cast<std::size_t>(k)]];
      if (isUpDown(colour)) {
        if (ori != -1) {
          throw InvalidStateError(
              "corner slot " + std::to_string(slot) +
              " shows two U/D colours; that corner piece does not exist",
              CubeFault::CornerPiece);
        }
        ori = k;
      }
    }
    if (ori == -1) {
      throw InvalidStateError(
          "corner slot " + std::to_string(slot) +
          " shows no U or D colour; that corner piece does not exist",
          CubeFault::CornerPiece);
    }

    const Face a =
        facelets[kCornerFacelets[static_cast<std::size_t>(slot)][static_cast<std::size_t>(ori)]];
    const Face b = facelets[kCornerFacelets[static_cast<std::size_t>(slot)]
                                           [static_cast<std::size_t>((ori + 1) % 3)]];
    const Face c = facelets[kCornerFacelets[static_cast<std::size_t>(slot)]
                                           [static_cast<std::size_t>((ori + 2) % 3)]];

    int found = -1;
    for (int j = 0; j < kNumCorners; ++j) {
      if (cornerColours[static_cast<std::size_t>(j)][0] == a &&
          cornerColours[static_cast<std::size_t>(j)][1] == b &&
          cornerColours[static_cast<std::size_t>(j)][2] == c) {
        found = j;
        break;
      }
    }
    if (found == -1) {
      throw InvalidStateError(
          std::string("corner slot ") + std::to_string(slot) + " shows colours " +
          faceLetter(a) + faceLetter(b) + faceLetter(c) +
          ", which is not a real corner piece", CubeFault::CornerPiece);
    }
    cp[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(found);
    co[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(ori);
  }

  for (int slot = 0; slot < kNumEdges; ++slot) {
    const Face a = facelets[kEdgeFacelets[static_cast<std::size_t>(slot)][0]];
    const Face b = facelets[kEdgeFacelets[static_cast<std::size_t>(slot)][1]];

    int found = -1;
    int ori = 0;
    for (int j = 0; j < kNumEdges; ++j) {
      if (edgeColours[static_cast<std::size_t>(j)][0] == a &&
          edgeColours[static_cast<std::size_t>(j)][1] == b) {
        found = j;
        ori = 0;
        break;
      }
      if (edgeColours[static_cast<std::size_t>(j)][0] == b &&
          edgeColours[static_cast<std::size_t>(j)][1] == a) {
        found = j;
        ori = 1;
        break;
      }
    }
    if (found == -1) {
      throw InvalidStateError(std::string("edge slot ") + std::to_string(slot) +
                              " shows colours " + faceLetter(a) +
                              faceLetter(b) + ", which is not a real edge piece",
                              CubeFault::EdgePiece);
    }
    ep[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(found);
    eo[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(ori);
  }

  // The sticker set can be self-consistent yet still unreachable (a twisted
  // corner, a flipped edge, or a swapped pair). Cube::validate covers those.
  const Cube cube = Cube::fromCubies(cp, co, ep, eo);
  cube.validate();
  return cube;
}

FaceletArray parseFacelets(std::string_view text) {
  std::string compact;
  compact.reserve(kNumFacelets);
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0) compact.push_back(ch);
  }
  if (compact.size() != kNumFacelets) {
    throw ParseError("expected " + std::to_string(kNumFacelets) +
                     " facelet characters, got " +
                     std::to_string(compact.size()));
  }

  FaceletArray out{};
  for (std::size_t i = 0; i < kNumFacelets; ++i) {
    const char ch =
        static_cast<char>(std::toupper(static_cast<unsigned char>(compact[i])));
    auto pos = kFaceLetters.find(ch);
    if (pos == std::string_view::npos) pos = kColourLetters.find(ch);
    if (pos == std::string_view::npos) {
      throw ParseError(std::string("invalid facelet character '") + compact[i] +
                       "' at position " + std::to_string(i) +
                       " (expected one of URFDLB or WRGYOB)");
    }
    out[i] = static_cast<Face>(pos);
  }
  return out;
}

std::string toFaceletString(const FaceletArray& facelets) {
  std::string out;
  out.reserve(kNumFacelets);
  for (const Face f : facelets) out.push_back(faceLetter(f));
  return out;
}

std::string toNetString(const FaceletArray& facelets) {
  const auto row = [&](int faceIdx, int r) {
    std::string s;
    for (int c = 0; c < 3; ++c) {
      s.push_back(faceLetter(facelets[static_cast<std::size_t>(faceIdx * 9 + r * 3 + c)]));
      if (c != 2) s.push_back(' ');
    }
    return s;
  };

  constexpr int U = 0, R = 1, F = 2, D = 3, L = 4, B = 5;
  const std::string pad = "        ";  // width of one face plus the separator

  std::string out;
  for (int r = 0; r < 3; ++r) out += pad + row(U, r) + "\n";
  out += "\n";
  for (int r = 0; r < 3; ++r) {
    out += row(L, r) + " " + row(F, r) + " " + row(R, r) + " " + row(B, r) + "\n";
  }
  out += "\n";
  for (int r = 0; r < 3; ++r) out += pad + row(D, r) + "\n";
  return out;
}

}  // namespace rubik
