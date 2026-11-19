#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"

TEST_CASE ("State save / restore round-trips every parameter", "[state]")
{
    TgsoundsEQAudioProcessor a;
    juce::Random rng (1234);
    for (auto* p : a.getParameters())
        p->setValueNotifyingHost (rng.nextFloat());

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    TgsoundsEQAudioProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());

    const auto& pa = a.getParameters();
    const auto& pb = b.getParameters();
    REQUIRE (pa.size() == pb.size());
    for (int i = 0; i < pa.size(); ++i)
    {
        INFO (pa[i]->getName (64));
        // Compare the quantised/plain value (bools & choices snap; floats round-trip exactly).
        REQUIRE (pb[i]->getCurrentValueAsText() == pa[i]->getCurrentValueAsText());
        if (dynamic_cast<juce::AudioParameterFloat*> (pa[i]) != nullptr)
            REQUIRE_THAT (pb[i]->getValue(), Catch::Matchers::WithinAbs (pa[i]->getValue(), 1e-6));
    }
}

TEST_CASE ("Restoring garbage state is harmless", "[state]")
{
    TgsoundsEQAudioProcessor p;
    const char junk[] = "not a valid state";
    p.setStateInformation (junk, (int) sizeof (junk));
    CHECK (p.getAPVTS().getParameter ("b01_freq") != nullptr);
}

TEST_CASE ("Bool parameters snap, so a restored state is exact (pluginval strictness 10)", "[state]")
{
    // Reproduces pluginval's "Plugin state restoration": random values -> save -> other random values -> restore.
    TgsoundsEQAudioProcessor p;
    juce::Random rng (99);
    for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (rng.nextFloat());
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    std::vector<float> expected;
    for (auto* prm : p.getParameters()) expected.push_back (prm->getValue());
    for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (rng.nextFloat());
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    for (int i = 0; i < p.getParameters().size(); ++i)
    {
        INFO (p.getParameters()[i]->getName (64));
        REQUIRE_THAT (p.getParameters()[i]->getValue(), Catch::Matchers::WithinAbs (expected[(size_t) i], 0.01));
    }
}
