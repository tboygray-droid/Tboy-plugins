#include "CurveFitter.h"
#include "dsp/FilterDesign.h"
#include <cmath>

namespace tg::fit
{
using params::Shape;

namespace
{
    struct Band { Shape shape; double logF, gain, logQ; };

    void bandDb (const Band& b, const std::vector<double>& freqs, std::vector<double>& out)
    {
        dsp::BandDesign d { b.shape, params::Slope::dB12, std::exp (b.logF), b.gain, std::exp (b.logQ) };
        const auto a = dsp::makeAnalog (d);
        out.resize (freqs.size());
        for (size_t i = 0; i < freqs.size(); ++i)
            out[i] = a.magnitudeDb (freqs[i]);
    }

    void clampBand (Band& b)
    {
        b.logF = std::clamp (b.logF, std::log (20.0), std::log (20000.0));
        b.gain = std::clamp (b.gain, (double) params::kGainMin, (double) params::kGainMax);
        b.logQ = std::clamp (b.logQ, std::log (0.2), std::log (16.0));
    }

    struct Model
    {
        const std::vector<double>& f;
        const std::vector<double>& target;
        const std::vector<double>& w;
        std::vector<Band> bands;
        std::vector<std::vector<double>> resp;   // per-band dB

        void recompute (size_t i) { bandDb (bands[i], f, resp[i]); }
        void add (const Band& b) { bands.push_back (b); resp.emplace_back(); recompute (bands.size() - 1); }

        std::vector<double> residual() const
        {
            std::vector<double> r (target);
            for (const auto& rb : resp)
                for (size_t i = 0; i < r.size(); ++i)
                    r[i] -= rb[i];
            return r;
        }

        double cost() const
        {
            const auto r = residual();
            double c = 0;
            for (size_t i = 0; i < r.size(); ++i)
                c += w[i] * r[i] * r[i];
            return c;
        }
    };

    /** Solve (A) x = b in place (Gaussian elimination with partial pivoting). */
    bool solve (std::vector<double>& A, std::vector<double>& b, int n)
    {
        for (int c = 0; c < n; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < n; ++r)
                if (std::abs (A[(size_t) (r * n + c)]) > std::abs (A[(size_t) (piv * n + c)])) piv = r;
            if (std::abs (A[(size_t) (piv * n + c)]) < 1e-15) return false;
            if (piv != c)
            {
                for (int k = 0; k < n; ++k) std::swap (A[(size_t) (c * n + k)], A[(size_t) (piv * n + k)]);
                std::swap (b[(size_t) c], b[(size_t) piv]);
            }
            for (int r = c + 1; r < n; ++r)
            {
                const double m = A[(size_t) (r * n + c)] / A[(size_t) (c * n + c)];
                for (int k = c; k < n; ++k) A[(size_t) (r * n + k)] -= m * A[(size_t) (c * n + k)];
                b[(size_t) r] -= m * b[(size_t) c];
            }
        }
        for (int r = n - 1; r >= 0; --r)
        {
            double s = b[(size_t) r];
            for (int k = r + 1; k < n; ++k) s -= A[(size_t) (r * n + k)] * b[(size_t) k];
            b[(size_t) r] = s / A[(size_t) (r * n + r)];
        }
        return true;
    }

    void refine (Model& m, int iterations)
    {
        const size_t nb = m.bands.size();
        const int np = (int) nb * 3;
        const size_t npts = m.f.size();
        double lambda = 1e-2;
        double cost = m.cost();

        std::vector<double> J ((size_t) np * npts);
        std::vector<double> tmp;
        for (int it = 0; it < iterations; ++it)
        {
            // Jacobian by forward differences, per band (only that band's curve changes).
            const auto r = m.residual();
            for (size_t b = 0; b < nb; ++b)
                for (int p = 0; p < 3; ++p)
                {
                    Band t = m.bands[b];
                    const double h = p == 1 ? 0.05 : 0.01;
                    (p == 0 ? t.logF : p == 1 ? t.gain : t.logQ) += h;
                    bandDb (t, m.f, tmp);
                    double* col = &J[(size_t) ((int) b * 3 + p) * npts];
                    for (size_t i = 0; i < npts; ++i)
                        col[i] = (tmp[i] - m.resp[b][i]) / h;
                }

            // Normal equations: (J^T W J + lambda diag) dx = J^T W r
            std::vector<double> A ((size_t) (np * np), 0.0), g ((size_t) np, 0.0);
            for (int a = 0; a < np; ++a)
            {
                const double* ca = &J[(size_t) a * npts];
                for (size_t i = 0; i < npts; ++i) g[(size_t) a] += ca[i] * m.w[i] * r[i];
                for (int c = a; c < np; ++c)
                {
                    const double* cc = &J[(size_t) c * npts];
                    double s = 0;
                    for (size_t i = 0; i < npts; ++i) s += ca[i] * m.w[i] * cc[i];
                    A[(size_t) (a * np + c)] = A[(size_t) (c * np + a)] = s;
                }
            }

            bool improved = false;
            for (int attempt = 0; attempt < 6 && ! improved; ++attempt)
            {
                auto Ad = A;
                auto dx = g;
                for (int a = 0; a < np; ++a) Ad[(size_t) (a * np + a)] *= 1.0 + lambda;
                for (int a = 0; a < np; ++a) Ad[(size_t) (a * np + a)] += 1e-9;
                if (! solve (Ad, dx, np)) { lambda *= 10; continue; }

                auto saved = m.bands;
                auto savedResp = m.resp;
                for (size_t b = 0; b < nb; ++b)
                {
                    m.bands[b].logF += dx[b * 3 + 0];
                    m.bands[b].gain += dx[b * 3 + 1];
                    m.bands[b].logQ += dx[b * 3 + 2];
                    clampBand (m.bands[b]);
                    m.recompute (b);
                }
                const double newCost = m.cost();
                if (newCost < cost)
                {
                    cost = newCost;
                    lambda = std::max (1e-6, lambda * 0.3);
                    improved = true;
                }
                else
                {
                    m.bands = saved;
                    m.resp = savedResp;
                    lambda *= 10;
                }
            }
            if (! improved)
                break;
        }
    }
} // namespace

std::vector<double> smoothOctaves (const std::vector<double>& f, const std::vector<double>& db, double fraction)
{
    std::vector<double> out (db.size());
    const double half = std::pow (2.0, 0.5 / fraction);
    for (size_t i = 0; i < f.size(); ++i)
    {
        double s = 0; int n = 0;
        for (size_t k = 0; k < f.size(); ++k)
            if (f[k] >= f[i] / half && f[k] <= f[i] * half) { s += db[k]; ++n; }
        out[i] = n > 0 ? s / n : db[i];
    }
    return out;
}

FitResult fitCurve (const std::vector<double>& freqs, const std::vector<double>& targetDb,
                    const std::vector<double>& weightsIn, const FitOptions& opt)
{
    std::vector<double> weights = weightsIn.empty() ? std::vector<double> (freqs.size(), 1.0) : weightsIn;
    Model m { freqs, targetDb, weights, {}, {} };
    const size_t n = freqs.size();

    for (int k = 0; k < opt.maxBands; ++k)
    {
        const auto r = m.residual();
        size_t peak = 0;
        double best = 0.0;
        for (size_t i = 0; i < n; ++i)
            if (weights[i] > 0 && std::abs (r[i]) > best) { best = std::abs (r[i]); peak = i; }
        if (best < opt.toleranceDb)
            break;

        // Width at half the residual (same sign).
        const double g = r[peak];
        size_t lo = peak, hi = peak;
        while (lo > 0 && r[lo - 1] * g > 0 && std::abs (r[lo - 1]) > 0.5 * std::abs (g)) --lo;
        while (hi + 1 < n && r[hi + 1] * g > 0 && std::abs (r[hi + 1]) > 0.5 * std::abs (g)) ++hi;
        const double octaves = std::max (0.15, std::log2 (freqs[hi] / freqs[lo]));
        const double twoN = std::pow (2.0, octaves);
        const double q = std::sqrt (twoN) / (twoN - 1.0);

        Band cand { Shape::Bell, std::log (freqs[peak]), g, std::log (std::clamp (q, 0.2, 16.0)) };
        clampBand (cand);

        // At the edges, a shelf usually explains the residual better: try it and keep the best.
        std::vector<Band> options { cand };
        if (lo == 0)
            options.push_back ({ Shape::LowShelf, std::log (freqs[std::min (hi, n - 1)]), g, std::log (0.7) });
        if (hi == n - 1)
            options.push_back ({ Shape::HighShelf, std::log (freqs[lo]), g, std::log (0.7) });

        double bestCost = 1e300;
        Band chosen = cand;
        for (auto o : options)
        {
            clampBand (o);
            m.add (o);
            const double c = m.cost();
            if (c < bestCost) { bestCost = c; chosen = o; }
            m.bands.pop_back();
            m.resp.pop_back();
        }
        m.add (chosen);
        refine (m, std::max (4, opt.lmIterations / 3));
    }
    refine (m, opt.lmIterations);

    FitResult res;
    const auto r = m.residual();
    double sum = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
        if (weights[i] > 0)
        {
            res.maxErrorDb = std::max (res.maxErrorDb, std::abs (r[i]));
            sum += weights[i] * r[i] * r[i];
            wsum += weights[i];
        }
    res.rmsErrorDb = wsum > 0 ? std::sqrt (sum / wsum) : 0.0;

    for (const auto& b : m.bands)
    {
        if (std::abs (b.gain) < 0.05)
            continue;
        params::BandSnapshot s;
        s.used = true;
        s.enabled = true;
        s.shape = b.shape;
        s.freq = (float) std::exp (b.logF);
        s.gain = (float) b.gain;
        s.q = (float) std::exp (b.logQ);
        s.slope = params::Slope::dB12;
        res.bands.push_back (s);
    }
    return res;
}
} // namespace tg::fit
