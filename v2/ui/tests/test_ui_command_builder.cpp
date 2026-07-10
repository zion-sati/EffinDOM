#include "TestUiSupport.h"

TEST_CASE("v2 ui command builder bit-casts floats, deletes, and glyph runs into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetBounds(42ULL, 10.5f, 20.0f, 100.0f, 50.0f);
    builder.DeleteNode(0x0000000200000001ULL);
    builder.SetTextFade(42ULL, ED_FADE_BOTTOM);
    builder.SetGlyphRun(42ULL, 7U, 24.0f, 0xff112233U, {
        effindom::v2::ui::GlyphPlacement{10U, 0.0f, 20.0f},
        effindom::v2::ui::GlyphPlacement{11U, 12.5f, 20.0f},
    });
    builder.SetBoxStyle(42ULL, 0xff223344U, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0xff556677U, ED_BORDER_DASHED, 6.0f, 7.0f);
    builder.SetLayerEffect(42ULL, 0.5f, 2.0f, ED_BLEND_MULTIPLY);
    builder.SetLinearGradient(42ULL, 8.0f, 9.0f, 10.0f, 11.0f, {
        effindom::v2::ui::GradientStop{0.0f, 0xff0000ffU},
        effindom::v2::ui::GradientStop{1.0f, 0x00ff00ffU},
    });

    REQUIRE(words.size() == 69U);
    CHECK(words[0] == CMD_SET_BOUNDS);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == effindom::v2::ui::CommandBuilder::FloatToWord(10.5f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[3]) == Approx(10.5f));
    CHECK(words[16] == CMD_DELETE_NODE);
    CHECK(words[17] == 1U);
    CHECK(words[18] == 2U);
    CHECK(words[19] == CMD_SET_TEXT_FADE);
    CHECK(words[22] == ED_FADE_BOTTOM);
    CHECK(words[23] == CMD_SET_GLYPH_RUN);
    CHECK(words[26] == 7U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[27]) == Approx(24.0f));
    CHECK(words[29] == 2U);
    CHECK(words[38] == CMD_SET_BOX_STYLE);
    CHECK(words[41] == 0xff223344U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[42]) == Approx(1.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[46]) == Approx(5.0f));
    CHECK(words[48] == ED_BORDER_DASHED);
    CHECK(words[51] == CMD_SET_LAYER_EFFECT);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[54]) == Approx(0.5f));
    CHECK(words[56] == ED_BLEND_MULTIPLY);
    CHECK(words[57] == CMD_SET_LINEAR_GRADIENT);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[60]) == Approx(8.0f));
    CHECK(words[64] == 2U);
    CHECK(words[66] == 0xff0000ffU);
    CHECK(words[68] == 0x00ff00ffU);
}


TEST_CASE("v2 ui command builder encodes background blur into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetBackgroundBlur(42ULL, 6.5f);

    REQUIRE(words.size() == 4U);
    CHECK(words[0] == CMD_SET_BACKGROUND_BLUR);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[3]) == Approx(6.5f));
}


TEST_CASE("v2 ui command builder encodes colored glyph runs into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetGlyphRunColored(42ULL, 7U, 24.0f, {
        effindom::v2::ui::GlyphPlacement{10U, 0.0f, 20.0f, 0U, 7U, 0xff0000ffU},
        effindom::v2::ui::GlyphPlacement{11U, 12.5f, 20.0f, 1U, 7U, 0x00ff00ffU},
    });

    REQUIRE(words.size() == 16U);
    CHECK(words[0] == CMD_SET_GLYPH_RUN_COLORED);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == 7U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[4]) == Approx(24.0f));
    CHECK(words[5] == 2U);
    CHECK(words[10] == 0xff0000ffU);
    CHECK(words[15] == 0x00ff00ffU);
}

TEST_CASE("v2 ui command builder encodes styled glyph runs into Core words", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetGlyphRunStyled(42ULL, 7U, 24.0f, {
        effindom::v2::ui::GlyphPlacement{10U, 0.0f, 20.0f, 0U, 7U, 0xff0000ffU, 24.0f},
        effindom::v2::ui::GlyphPlacement{11U, 12.5f, 20.0f, 1U, 7U, 0x00ff00ffU, 32.0f},
    });

    REQUIRE(words.size() == 18U);
    CHECK(words[0] == CMD_SET_GLYPH_RUN_STYLED);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == 7U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[4]) == Approx(24.0f));
    CHECK(words[5] == 2U);
    CHECK(words[10] == 0xff0000ffU);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[11]) == Approx(24.0f));
    CHECK(words[16] == 0x00ff00ffU);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[17]) == Approx(32.0f));
}


TEST_CASE("v2 ui command builder encodes retained image and svg commands", "[v2][ui][unit]") {
    std::vector<std::uint32_t> words{};
    effindom::v2::ui::CommandBuilder builder(words);

    builder.SetImage(42ULL, 17U, ED_OBJECT_FIT_CONTAIN, ED_IMAGE_SAMPLING_NEAREST, 0U);
    builder.SetImageNine(42ULL, 18U, 1.0f, 2.0f, 3.0f, 4.0f, ED_IMAGE_SAMPLING_CUBIC_MITCHELL, 0U);
    builder.SetSvg(42ULL, 19U, 0xff3366ffU, ED_IMAGE_SAMPLING_ANISOTROPIC, 12U);

    REQUIRE(words.size() == 24U);
    CHECK(words[0] == CMD_SET_IMAGE);
    CHECK(words[1] == 42U);
    CHECK(words[2] == 0U);
    CHECK(words[3] == 17U);
    CHECK(words[4] == ED_OBJECT_FIT_CONTAIN);
    CHECK(words[5] == ED_IMAGE_SAMPLING_NEAREST);
    CHECK(words[6] == 0U);
    CHECK(words[7] == CMD_SET_IMAGE_NINE);
    CHECK(words[10] == 18U);
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[11]) == Approx(1.0f));
    CHECK(effindom::v2::ui::CommandBuilder::WordToFloat(words[14]) == Approx(4.0f));
    CHECK(words[15] == ED_IMAGE_SAMPLING_CUBIC_MITCHELL);
    CHECK(words[16] == 0U);
    CHECK(words[17] == CMD_SET_SVG);
    CHECK(words[20] == 19U);
    CHECK(words[21] == 0xff3366ffU);
    CHECK(words[22] == ED_IMAGE_SAMPLING_ANISOTROPIC);
    CHECK(words[23] == 12U);
}
