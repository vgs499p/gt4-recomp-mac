#pragma once
// macOS / POSIX port: the XInput state types and constants that the pad mapping API and its fixtures use, with the
// same layout and values as <Xinput.h>. There is no XInput device on these platforms; the SDL3 gamepad backend reads
// pads and fills the same XInput-shaped reading (host_io.cpp). On Windows this header is empty.

#ifndef _WIN32

#include "controls.hpp"

#include <cstdint>

using WORD = std::uint16_t;
using BYTE = std::uint8_t;
using SHORT = std::int16_t;

struct XINPUT_GAMEPAD {
  WORD wButtons;
  BYTE bLeftTrigger;
  BYTE bRightTrigger;
  SHORT sThumbLX;
  SHORT sThumbLY;
  SHORT sThumbRX;
  SHORT sThumbRY;
};

struct XINPUT_VIBRATION {
  WORD wLeftMotorSpeed;
  WORD wRightMotorSpeed;
};

inline constexpr WORD XINPUT_GAMEPAD_DPAD_UP = gt4::controls::xinput::dpad_up;
inline constexpr WORD XINPUT_GAMEPAD_DPAD_DOWN = gt4::controls::xinput::dpad_down;
inline constexpr WORD XINPUT_GAMEPAD_DPAD_LEFT = gt4::controls::xinput::dpad_left;
inline constexpr WORD XINPUT_GAMEPAD_DPAD_RIGHT = gt4::controls::xinput::dpad_right;
inline constexpr WORD XINPUT_GAMEPAD_START = gt4::controls::xinput::start;
inline constexpr WORD XINPUT_GAMEPAD_BACK = gt4::controls::xinput::back;
inline constexpr WORD XINPUT_GAMEPAD_LEFT_THUMB = gt4::controls::xinput::left_thumb;
inline constexpr WORD XINPUT_GAMEPAD_RIGHT_THUMB = gt4::controls::xinput::right_thumb;
inline constexpr WORD XINPUT_GAMEPAD_LEFT_SHOULDER = gt4::controls::xinput::left_shoulder;
inline constexpr WORD XINPUT_GAMEPAD_RIGHT_SHOULDER = gt4::controls::xinput::right_shoulder;
inline constexpr WORD XINPUT_GAMEPAD_A = gt4::controls::xinput::a;
inline constexpr WORD XINPUT_GAMEPAD_B = gt4::controls::xinput::b;
inline constexpr WORD XINPUT_GAMEPAD_X = gt4::controls::xinput::x;
inline constexpr WORD XINPUT_GAMEPAD_Y = gt4::controls::xinput::y;
inline constexpr SHORT XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE = 7849;
inline constexpr SHORT XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE = 8689;
inline constexpr BYTE XINPUT_GAMEPAD_TRIGGER_THRESHOLD = 30;
inline constexpr unsigned XUSER_MAX_COUNT = 4;

#endif
