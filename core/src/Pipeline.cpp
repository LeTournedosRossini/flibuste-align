#include "align/Pipeline.h"

namespace align
{
Analysis analysePair (const float* g, size_t nG, const float* d, size_t nD, double sr, const AlignSettings& a, const AnalysisSettings& an)
{
    Analysis out;
    out.sampleRate = sr;
    out.length = nG;
    out.path = alignSignals (g, nG, d, nD, sr, a, an);
    out.consonants = findConsonants (d, nD, sr);
    return out;
}

namespace
{
std::vector<double> strengthPath (const Analysis& a, const RenderSettings& r)
{
    return r.strengthCurve.empty() ? applyStrength (a.path, a.hopSeconds, r.strength) : applyStrength (a.path, a.hopSeconds, r.strengthCurve);
}
} // namespace

RenderResult renderAnalysis (const std::vector<std::vector<float>>& dub, const Analysis& a, const RenderSettings& r)
{
    const auto path = strengthPath (a, r);
    const auto map = makeWarpMap (path, a.hopSeconds, a.sampleRate, a.length, a.consonants, r.protect);
    return render (dub, map, a.sampleRate, r.engine);
}

std::vector<double> appliedShift (const Analysis& a, const RenderSettings& r)
{
    const auto path = strengthPath (a, r);
    std::vector<double> s (path.size());
    for (size_t i = 0; i < path.size(); ++i) s[i] = path[i] - (double) i * a.hopSeconds;
    return s;
}
} // namespace align
