#pragma once

#include <cmath>

namespace kisa {
inline constexpr double ScoreScale = 1000.0;
// Allow binary float32/double representation error, well below a fourth decimal.
inline constexpr double ScoreTolerance = 1e-7;

inline double RoundScore(double score) {
    return std::round(score * ScoreScale) / ScoreScale;
}

inline bool ValidScore(double score) {
    return std::isfinite(score) && score >= 0.0 && score <= 1.0
        && std::abs(score - RoundScore(score)) <= ScoreTolerance;
}
} // namespace kisa
