#include "framework.h"

#include "sequencer/TransportTimeline.h"

using sequencer::TransportTimeline;

// ui-integration T2: the global scale (root + template index) lives in ONE
// place — the TransportTimeline — and is modulatable with Global scope
// alongside BPM (FR12, FR13, FR14, AC10, AC11).

TEST(transporttimeline_default_scale_is_c_major) {
    TransportTimeline t;
    CHECK_EQ(t.scaleRoot(), 0);            // default root C
    CHECK_EQ(t.scaleTemplateIndex(), 0);   // default template: Major
    CHECK_EQ(t.scale().root(), 0);
    CHECK_EQ(t.scale().templateIndex(), 0);
}

TEST(transporttimeline_set_scale_stores_root_and_template) {
    TransportTimeline t;
    CHECK(t.setScale(7, 1));  // G Natural Minor
    CHECK_EQ(t.scaleRoot(), 7);
    CHECK_EQ(t.scaleTemplateIndex(), 1);
    CHECK(t.scale() == common::Scale(7, 1));

    CHECK(t.setScale(11, 0));  // B Major
    CHECK_EQ(t.scaleRoot(), 11);
    CHECK_EQ(t.scaleTemplateIndex(), 0);
    CHECK(t.scale() == common::Scale(11, 0));
}

TEST(transporttimeline_set_scale_rejects_out_of_range_and_keeps_previous) {
    TransportTimeline t;
    CHECK(t.setScale(3, 2));
    CHECK_EQ(t.scaleRoot(), 3);
    CHECK_EQ(t.scaleTemplateIndex(), 2);

    const uint8_t templateCount =
        static_cast<uint8_t>(common::scaleTemplateCount());

    CHECK(!t.setScale(12, 0));   // root must be a pitch class 0-11
    CHECK(!t.setScale(255, 0));
    CHECK(!t.setScale(0, templateCount));      // one past the last template
    CHECK(!t.setScale(0, 200));                // far out of range
    CHECK_EQ(t.scaleRoot(), 3);                // timeline unchanged
    CHECK_EQ(t.scaleTemplateIndex(), 2);
    CHECK(t.scale() == common::Scale(3, 2));
}

TEST(transporttimeline_set_scale_accepts_range_boundaries) {
    TransportTimeline t;
    const uint8_t lastTemplate =
        static_cast<uint8_t>(common::scaleTemplateCount()) - 1;
    CHECK(t.setScale(11, lastTemplate));
    CHECK_EQ(t.scaleRoot(), 11);
    CHECK_EQ(t.scaleTemplateIndex(), lastTemplate);
    CHECK(t.scale() == common::Scale(11, lastTemplate));
}

TEST(transporttimeline_scale_is_independent_of_transport_state) {
    // The scale is a true global setting on the timeline: setting BPM or the
    // meter must not disturb it, and a scale change must not disturb them.
    TransportTimeline t;
    CHECK(t.setBpm(150));
    CHECK(t.setTimeSignature(common::TimeSignature(3, 4)));
    CHECK(t.setScale(9, 3));
    CHECK_EQ(t.bpm(), 150);
    CHECK(t.timeSignature() == common::TimeSignature(3, 4));
    CHECK_EQ(t.scaleRoot(), 9);
    CHECK_EQ(t.scaleTemplateIndex(), 3);
}
