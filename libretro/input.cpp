#include "PrecompiledHeader.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <libretro.h>

#include "input.h"
//#include "PS2Edefs.h"

#include "PAD/Host/StateManagement.h"
#include "PAD/Host/KeyStatus.h"
#include "Frontend/InputManager.h"

extern retro_environment_t environ_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
struct retro_rumble_interface rumble;

//PADconf g_conf;

static struct retro_input_descriptor desc[] = {
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "L-Analog X"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "L-Analog Y"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "R-Analog X"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "R-Analog Y"},

	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
	{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},
	{1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "L-Analog X"},
	{1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "L-Analog Y"},
	{1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "R-Analog X"},
	{1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "R-Analog Y"},

	{0},
};

namespace Input
{

void Init()
{
	environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble);
	static const struct retro_controller_description ds2_desc[] = {
		{"DualShock 2", RETRO_DEVICE_ANALOG},
	};

	static const struct retro_controller_info ports[] = {
		{ds2_desc, sizeof(ds2_desc) / sizeof(*ds2_desc)},
		{ds2_desc, sizeof(ds2_desc) / sizeof(*ds2_desc)},
		{},
	};

	environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
	//	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

void Shutdown()
{
}

void Update()
{
	poll_cb();
#ifdef __ANDROID__
	/* Android doesn't support input polling on all threads by default
   * this will force the poll for this frame to happen in the main thread
   * in case the frontend is doing late-polling */
	input_cb(0, 0, 0, 0);
#endif
	Pad::rumble_all();
}

} // namespace Input

void retro_set_input_poll(retro_input_poll_t cb)
{
	poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
	input_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

//void Device::DoRumble(unsigned type, unsigned pad)
//{
//	if (pad >= GAMEPAD_NUMBER)
//		return;

//	if (type == 0)
//		rumble.set_rumble_state(pad, RETRO_RUMBLE_WEAK, 0xFFFF);
//	else
//		rumble.set_rumble_state(pad, RETRO_RUMBLE_STRONG, 0xFFFF);
//}

static int keymap[] =
	{
		RETRO_DEVICE_ID_JOYPAD_L2,     // PAD_L2
		RETRO_DEVICE_ID_JOYPAD_R2,     // PAD_R2
		RETRO_DEVICE_ID_JOYPAD_L,      // PAD_L1
		RETRO_DEVICE_ID_JOYPAD_R,      // PAD_R1
		RETRO_DEVICE_ID_JOYPAD_X,      // PAD_TRIANGLE
		RETRO_DEVICE_ID_JOYPAD_A,      // PAD_CIRCLE
		RETRO_DEVICE_ID_JOYPAD_B,      // PAD_CROSS
		RETRO_DEVICE_ID_JOYPAD_Y,      // PAD_SQUARE
		RETRO_DEVICE_ID_JOYPAD_SELECT, // PAD_SELECT
		RETRO_DEVICE_ID_JOYPAD_L3,     // PAD_L3
		RETRO_DEVICE_ID_JOYPAD_R3,     // PAD_R3
		RETRO_DEVICE_ID_JOYPAD_START,  // PAD_START
		RETRO_DEVICE_ID_JOYPAD_UP,     // PAD_UP
		RETRO_DEVICE_ID_JOYPAD_RIGHT,  // PAD_RIGHT
		RETRO_DEVICE_ID_JOYPAD_DOWN,   // PAD_DOWN
		RETRO_DEVICE_ID_JOYPAD_LEFT,   // PAD_LEFT
};


u32 PAD::KeyStatus::GetButtons(u32 pad)
{
	u32 mask = input_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
	u32 new_mask = 0xFFFF0000;
	for (int i = 0; i < 16; i++)
		new_mask |= !(mask & (1 << keymap[i])) << i;

	return new_mask;
}

u8 PAD::KeyStatus::GetPressure(u32 pad, u32 index)
{
	int val = 0;
	switch (index)
	{
		case PAD_R_LEFT:
		case PAD_R_RIGHT:
			val = input_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
			break;

		case PAD_R_DOWN:
		case PAD_R_UP:
			val = input_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
			break;

		case PAD_L_LEFT:
		case PAD_L_RIGHT:
			val = input_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
			break;

		case PAD_L_DOWN:
		case PAD_L_UP:
			val = input_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
			break;

		default:
			if (index < 16)
				val = input_cb(pad, RETRO_DEVICE_JOYPAD, 0, keymap[index]);
			break;
	}

	if (index < 16)
	{
#if 0
		return 0xFF - (val >> 7);
#else
		return val ? 0x00 : 0xFF;
#endif
	}

	return 0x80 + (val >> 8);
}

PAD::KeyStatus::KeyStatus()
{
}

void PAD::KeyStatus::Init()
{
}

void PAD::KeyStatus::Set(u32 pad, u32 index, float value)
{
}

void InputManager::PollSources()
{
}
void InputManager::CloseSources()
{
}

void InputManager::ReloadSources(SettingsInterface& si, std::unique_lock<std::mutex>& settings_lock)
{
}

void InputManager::ReloadBindings(SettingsInterface& si, SettingsInterface& binding_si)
{
}
void InputManager::PauseVibration()
{
}

const char* InputManager::InputSourceToString(InputSourceType clazz)
{
	return "";
}
