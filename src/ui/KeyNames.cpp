#include "KeyNames.h"

namespace ui {
namespace {

struct Entry { KeyId id; const char* name; };

constexpr Entry NAMES[] = {
    {KeyId::A, "a"}, {KeyId::B, "b"}, {KeyId::C, "c"}, {KeyId::D, "d"},
    {KeyId::E, "e"}, {KeyId::F, "f"}, {KeyId::G, "g"}, {KeyId::H, "h"},
    {KeyId::I, "i"}, {KeyId::J, "j"}, {KeyId::K, "k"}, {KeyId::L, "l"},
    {KeyId::M, "m"}, {KeyId::N, "n"}, {KeyId::O, "o"}, {KeyId::P, "p"},
    {KeyId::Q, "q"}, {KeyId::R, "r"}, {KeyId::S, "s"}, {KeyId::T, "t"},
    {KeyId::U, "u"}, {KeyId::V, "v"}, {KeyId::W, "w"}, {KeyId::X, "x"},
    {KeyId::Y, "y"}, {KeyId::Z, "z"},

    {KeyId::NUM_1, "1"}, {KeyId::NUM_2, "2"}, {KeyId::NUM_3, "3"},
    {KeyId::NUM_4, "4"}, {KeyId::NUM_5, "5"}, {KeyId::NUM_6, "6"},
    {KeyId::NUM_7, "7"}, {KeyId::NUM_8, "8"}, {KeyId::NUM_9, "9"},
    {KeyId::NUM_0, "0"},

    {KeyId::ENTER, "enter"}, {KeyId::ESCAPE, "escape"},
    {KeyId::BACKSPACE, "backspace"}, {KeyId::TAB, "tab"},
    {KeyId::SPACE, "space"}, {KeyId::MINUS, "minus"}, {KeyId::EQUAL, "equal"},

    {KeyId::F1, "f1"},   {KeyId::F2, "f2"},   {KeyId::F3, "f3"},
    {KeyId::F4, "f4"},   {KeyId::F5, "f5"},   {KeyId::F6, "f6"},
    {KeyId::F7, "f7"},   {KeyId::F8, "f8"},   {KeyId::F9, "f9"},
    {KeyId::F10, "f10"}, {KeyId::F11, "f11"}, {KeyId::F12, "f12"},

    {KeyId::DELETE_KEY, "delete"},
    {KeyId::RIGHT, "right"}, {KeyId::LEFT, "left"},
    {KeyId::DOWN, "down"},   {KeyId::UP, "up"},
};

} // namespace

const char* toName(KeyId id)
{
    for (const auto& entry : NAMES) {
        if (entry.id == id) return entry.name;
    }
    return "?";
}

} // namespace ui
