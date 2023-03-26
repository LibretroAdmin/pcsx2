
#include "PrecompiledHeader.h"

#ifdef WIN32
#include <windows.h>
#undef Yield
#undef min
#undef max
#endif


#include <cstdint>
#include <libretro.h>
#include <string>
#include <thread>

#include "options.h"

#include "GS.h"
#include "input.h"
#include "svnrev.h"
#include "SPU2/Global.h"
#include "ps2/BiosTools.h"
#include "CDVD/CDVD.h"
#include "MTVU.h"
#include "Counters.h"
#include "Host.h"
#include "HostDisplay.h"
#include "HostSettings.h"
#include "common/StringUtil.h"
#include "common/Path.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/SafeArray.inl"

#include "pcsx2/Frontend/CommonHost.h"
#include "pcsx2/Frontend/FullscreenUI.h"
#include "pcsx2/Frontend/GameList.h"
#include "pcsx2/Frontend/InputManager.h"
#include "pcsx2/Frontend/ImGuiManager.h"
#include "pcsx2/Frontend/LogSink.h"
#include "pcsx2/Frontend/LayeredSettingsInterface.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/StateWrapper.h"
#include "pcsx2/GS/Renderers/Common/GSRenderer.h"

#include "SPU2/spu2.h"
#include "PAD/Host/PAD.h"

//#define PERF_TEST
#ifdef PERF_TEST
static struct retro_perf_callback perf_cb;

#define RETRO_PERFORMANCE_INIT(name)                 \
	retro_perf_tick_t current_ticks;                 \
	static struct retro_perf_counter name = {#name}; \
	if (!name.registered)                            \
		perf_cb.perf_register(&(name));              \
	current_ticks = name.total

#define RETRO_PERFORMANCE_START(name) perf_cb.perf_start(&(name))
#define RETRO_PERFORMANCE_STOP(name) \
	perf_cb.perf_stop(&(name));      \
	current_ticks = name.total - current_ticks;
#else
#define RETRO_PERFORMANCE_INIT(name)
#define RETRO_PERFORMANCE_START(name)
#define RETRO_PERFORMANCE_STOP(name)
#endif

namespace Options
{
static Option<std::string> bios("pcsx2_bios", "Bios"); // will be filled in retro_init()
static Option<bool> fast_boot("pcsx2_fastboot", "Fast Boot", true);

GfxOption<std::string> renderer("pcsx2_renderer", "Renderer", {"Auto", "OpenGL",
#ifdef _WIN32
															   "D3D11",
#endif
															   "Software", "Null"});

GfxOption<int> upscale_multiplier("pcsx2_upscale_multiplier", "Internal Resolution",
								  {{"Native PS2", 1}, {"2x Native ~720p", 2}, {"3x Native ~1080p", 3},{"4x Native ~1440p 2K", 4},
								   {"5x Native ~1620p 3K", 5}, {"6x Native ~2160p 4K", 6}, {"8x Native ~2880p 5K", 8}});
//static GfxOption<int> sw_renderer_threads("pcsx2_sw_renderer_threads", "Software Renderer Threads", 2, 10);
} // namespace Options

retro_environment_t environ_cb;
retro_video_refresh_t video_cb;
struct retro_hw_render_callback hw_render;
retro_log_printf_t log_cb;
static retro_audio_sample_batch_t batch_cb;
static retro_audio_sample_t sample_cb;

static const int samples_max = 0x800;
static int write_pos = 0;
static s16 snd_buffer[samples_max << 1];
static std::mutex snd_mutex;

static VMState cpu_thread_state;
static MemorySettingsInterface s_settings_interface;

static std::thread cpu_thread;
alignas(16) static SysMtgsThread s_mtgs_thread;

SysMtgsThread& GetMTGS()
{
	return s_mtgs_thread;
}

static void cpu_thread_pause()
{
	VMManager::SetPaused(true);
	while(cpu_thread_state != VMState::Paused)
		GetMTGS().Flush();
	GetMTGS().Flush();
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	batch_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
	sample_cb = cb;
}

bool SndBuffer::Init(const char* modname)
{
	write_pos = 0;
	return true;
}

void SndBuffer::Write(StereoOut16 Sample)
{
	std::unique_lock locker(snd_mutex);
	if (write_pos < (samples_max << 1))
	{
		snd_buffer[write_pos++] = Sample.Left;
		snd_buffer[write_pos++] = Sample.Right;
	}
}

void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
	bool no_game = true;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
#ifdef PERF_TEST
	environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb);
#endif
}

static std::vector<std::string> disk_images;
static int image_index = 0;

static bool RETRO_CALLCONV get_eject_state(void)
{
	return cdvdRead(0x0B);
}

static bool RETRO_CALLCONV set_eject_state(bool ejected)
{
	if (get_eject_state() == ejected)
		return false;

	cpu_thread_pause();

	if (ejected)
		cdvdCtrlTrayOpen();
	else
	{
		if (image_index < 0 || image_index >= (int)disk_images.size())
			VMManager::ChangeDisc(CDVD_SourceType::NoDisc, "");
		else
			VMManager::ChangeDisc(CDVD_SourceType::Iso, disk_images[image_index]);
//		cdvdCtrlTrayClose();
	}

	VMManager::SetPaused(false);
	return true;
}

static unsigned RETRO_CALLCONV get_image_index(void)
{
	return image_index;
}
static bool RETRO_CALLCONV set_image_index(unsigned index)
{
	if (get_eject_state())
	{
		image_index = index;
		return true;
	}

	return false;
}
static unsigned RETRO_CALLCONV get_num_images(void)
{
	return disk_images.size();
}

static bool RETRO_CALLCONV replace_image_index(unsigned index, const struct retro_game_info* info)
{
	if (index >= disk_images.size())
		return false;

	if (!info->path)
	{
		disk_images.erase(disk_images.begin() + index);
		if (!disk_images.size())
			image_index = -1;
		else if (image_index > (int)index)
			image_index--;
	}
	else
		disk_images[index] = info->path;

	return true;
}

static bool RETRO_CALLCONV add_image_index(void)
{
	disk_images.push_back("");
	return true;
}

static bool RETRO_CALLCONV set_initial_image(unsigned index, const char* path)
{
	if (index >= disk_images.size())
		index = 0;

	image_index = index;

	return true;
}

static bool RETRO_CALLCONV get_image_path(unsigned index, char* path, size_t len)
{
	if (index >= disk_images.size())
		return false;

	if (disk_images[index].empty())
		return false;

	strncpy(path, disk_images[index].c_str(), len);
	return true;
}
static bool RETRO_CALLCONV get_image_label(unsigned index, char* label, size_t len)
{
	if (index >= disk_images.size())
		return false;

	if (disk_images[index].empty())
		return false;

	strncpy(label, disk_images[index].c_str(), len);
	return true;
}

void retro_init(void)
{
	enum retro_pixel_format xrgb888 = RETRO_PIXEL_FORMAT_XRGB8888;
	environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &xrgb888);
	struct retro_log_callback log;
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
	{
		log_cb = log.log;
#if 0
		Console_SetActiveHandler(ConsoleWriter_Libretro);
#endif
	}

	vu1Thread.Reset();

	if (Options::bios.empty())
	{
		std::string bios_dir;
		const char* system = nullptr;
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system);
		bios_dir = Path::Combine(system, "/pcsx2/bios");

		FileSystem::FindResultsArray results;
		if (FileSystem::FindFiles(bios_dir.c_str(), "*", FILESYSTEM_FIND_FILES, &results))
		{
			static constexpr u32 MIN_BIOS_SIZE = 4 * _1mb;
			static constexpr u32 MAX_BIOS_SIZE = 8 * _1mb;
			u32 version, region;
			std::string description, zone;
			for (const FILESYSTEM_FIND_DATA& fd : results)
			{
				if (fd.Size < MIN_BIOS_SIZE || fd.Size > MAX_BIOS_SIZE)
					continue;

				if (IsBIOS(fd.FileName.c_str(), version, description, region, zone))
					Options::bios.push_back(description, std::string(Path::GetFileName(fd.FileName)));
			}
		}
	}

	Options::SetVariables();

	static retro_disk_control_ext_callback disk_control = {
		set_eject_state,
		get_eject_state,
		get_image_index,
		set_image_index,
		get_num_images,
		replace_image_index,
		add_image_index,
		set_initial_image,
		get_image_path,
		get_image_label,
	};

	environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disk_control);
}

void retro_deinit(void)
{
	// WIN32 doesn't allow canceling threads from global constructors/destructors in a shared library.
	vu1Thread.Close();
#ifdef PERF_TEST
	perf_cb.perf_log();
#endif
}

void retro_get_system_info(retro_system_info* info)
{
#ifdef GIT_REV
	info->library_version = GIT_REV;
#else
	static char version[] = "#.#.#";
	version[0] = '0' + PCSX2_VersionHi;
	version[2] = '0' + PCSX2_VersionMid;
	version[4] = '0' + PCSX2_VersionLo;
	info->library_version = version;
#endif

	info->library_name = "pcsx2";
	info->valid_extensions = "elf|iso|ciso|cue|bin|gz";
	info->need_fullpath = true;
	info->block_extract = true;
}

void retro_get_system_av_info(retro_system_av_info* info)
{
	if (Options::renderer == "Software" || Options::renderer == "Null")
	{
		info->geometry.base_width = 640;
		info->geometry.base_height = 480;
	}
	else
	{
		info->geometry.base_width = 640 * Options::upscale_multiplier;
		info->geometry.base_height = 480 * Options::upscale_multiplier;
	}

	info->geometry.max_width = info->geometry.base_width;
	info->geometry.max_height = info->geometry.base_height;

	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = (retro_get_region() == RETRO_REGION_NTSC) ? (60.0f / 1.001f) : 50.0f;
	info->timing.sample_rate = 48000;
}

void retro_reset(void)
{
	GetMTGS().Flush();
	VMManager::Reset();
	{
		std::unique_lock locker(snd_mutex);
		write_pos = 0;
	}
}

freezeData gs_freeze_data = {};

static void context_reset()
{
	printf("Context reset\n");

	WindowInfo wi;
	wi.surface_width = 640 * Options::upscale_multiplier;
	wi.surface_height = 480 * Options::upscale_multiplier;
	wi.surface_scale = 1.0f;
	wi.type = WindowInfo::Type::Libretro;

	RenderAPI api;
	switch(hw_render.context_type)
	{
#ifdef _WIN32
		case RETRO_HW_CONTEXT_DIRECT3D:
			if(hw_render.version_major == 11)
				api = RenderAPI::D3D11;
			else
				api = RenderAPI::D3D12;
			break;
#endif
		case RETRO_HW_CONTEXT_VULKAN:
			api = RenderAPI::Vulkan;
			break;

		default:
			api = RenderAPI::OpenGL;
			break;
	}

	g_host_display = HostDisplay::CreateForAPI(api);
	g_host_display->CreateDevice(wi, VsyncMode::Off);
	g_host_display->MakeCurrent();
	g_host_display->SetupDevice();

	s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", Options::upscale_multiplier);
//	VMManager::ApplySettings();
	GSConfig.UpscaleMultiplier = Options::upscale_multiplier;
	EmuConfig.GS.UpscaleMultiplier = Options::upscale_multiplier;
	GetMTGS().TryOpenGS();

	if (gs_freeze_data.data)
	{
		g_gs_renderer->Defrost(&gs_freeze_data);
		free(gs_freeze_data.data);
		gs_freeze_data = {};
	}

	VMManager::SetPaused(false);
}

static void context_destroy()
{
	cpu_thread_pause();

	pxAssert(!gs_freeze_data.data);
	g_gs_renderer->Freeze(&gs_freeze_data, true);
	gs_freeze_data.data = (u8*)malloc(gs_freeze_data.size);
	g_gs_renderer->Freeze(&gs_freeze_data, false);

	GetMTGS().CloseGS();
	printf("Context destroy\n");
}

static bool set_hw_render(retro_hw_context_type type)
{
	hw_render.context_type = type;
	hw_render.context_reset = context_reset;
	hw_render.context_destroy = context_destroy;
	hw_render.bottom_left_origin = true;
	hw_render.depth = true;
	hw_render.cache_context = true;

	switch (type)
	{
#ifdef _WIN32
		case RETRO_HW_CONTEXT_DIRECT3D:
			hw_render.version_major = 11;
			hw_render.version_minor = 0;
			break;
#endif
		case RETRO_HW_CONTEXT_OPENGL_CORE:
			hw_render.version_major = 3;
			hw_render.version_minor = 3;
			hw_render.cache_context = false;
			break;

		case RETRO_HW_CONTEXT_OPENGL:
			hw_render.version_major = 3;
			hw_render.version_minor = 0;
			break;

		case RETRO_HW_CONTEXT_OPENGLES3:
			hw_render.version_major = 3;
			hw_render.version_minor = 0;
			break;

		case RETRO_HW_CONTEXT_NONE:
			return true;

		default:
			return false;
	}

	return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render);
}

bool select_hw_render()
{
	if (Options::renderer == "Auto")
	{
		retro_hw_context_type context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
		environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &context_type);
		if (set_hw_render(context_type))
			return true;
	}
#ifdef _WIN32
	if (Options::renderer == "D3D11")
		return set_hw_render(RETRO_HW_CONTEXT_DIRECT3D);
#endif
	if (Options::renderer == "Null")
		return set_hw_render(RETRO_HW_CONTEXT_NONE);

	if (set_hw_render(RETRO_HW_CONTEXT_OPENGL_CORE))
		return true;
	if (set_hw_render(RETRO_HW_CONTEXT_OPENGL))
		return true;
	if (set_hw_render(RETRO_HW_CONTEXT_OPENGLES3))
		return true;
#ifdef _WIN32
	if (set_hw_render(RETRO_HW_CONTEXT_DIRECT3D))
		return true;
#endif

	return false;
}

static void executeVM()
{
	for (;;)
	{
		cpu_thread_state = VMManager::GetState();
		switch (VMManager::GetState())
		{
			case VMState::Initializing:
				pxFailRel("Shouldn't be in the starting state state");
				continue;

			case VMState::Paused:
				continue;

			case VMState::Running:
				VMManager::Execute();
				continue;

			case VMState::Resetting:
				VMManager::Reset();
				continue;

			case VMState::Stopping:
//				VMManager::Shutdown(false);
				return;

			default:
				continue;
		}
	}
}

void cpu_thread_entry(VMBootParameters boot_params)
{
	Threading::SetNameOfCurrentThread("CPU Thread");
//	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle::GetForCallingThread());

	VMManager::Initialize(boot_params);
	VMManager::SetState(VMState::Running);

	while (VMManager::GetState() != VMState::Shutdown)
	{
		if (!VMManager::HasValidVM())
			continue;

		executeVM();
	}

//	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
}

bool retro_load_game(const struct retro_game_info* game)
{
	const char* system_base = nullptr;
	environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_base);
	std::string system = Path::Combine(system_base, "pcsx2");

	EmuFolders::AppRoot = system;
	EmuFolders::Resources = Path::Combine(EmuFolders::AppRoot, "resources");
	EmuFolders::DataRoot = EmuFolders::AppRoot;
	CommonHost::InitializeCriticalFolders();

	Host::Internal::SetBaseSettingsLayer(&s_settings_interface);
	CommonHost::SetDefaultSettings(s_settings_interface, true, true, true, true, true);
	CommonHost::LoadStartupSettings();

	if (!VMManager::Internal::InitializeGlobals() || !VMManager::Internal::InitializeMemory())
		pxFailRel("Failed to allocate memory map");

	VMManager::LoadSettings();

	if (Options::bios.empty())
	{
		log_cb(RETRO_LOG_ERROR, "Could not find any valid PS2 Bios File in %s\n", EmuFolders::Bios.c_str());
		return false;
	}

	s_settings_interface.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
	s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", Options::upscale_multiplier);
	s_settings_interface.SetBoolValue("EmuCore", "EnableFastBoot", Options::fast_boot);
	s_settings_interface.SetStringValue("Filenames", "BIOS", Options::bios.Get().c_str());

	write_pos = 0;

	Input::Init();

//	g_host_display = HostDisplay::CreateForAPI(RenderAPI::OpenGL);
	Options::renderer.UpdateAndLock(); // disallow changes to Options::renderer outside of retro_load_game.

	if(!select_hw_render())
		return false;

	switch (hw_render.context_type)
	{
		case RETRO_HW_CONTEXT_DIRECT3D:
			s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)GSRendererType::DX11);
			break;
		case RETRO_HW_CONTEXT_NONE:
			s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)GSRendererType::Null);
			break;
		default:
			if(Options::renderer == "Software")
				s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)GSRendererType::SW);
			else
				s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)GSRendererType::OGL);
			break;
	}

	VMManager::ApplySettings();

	VMBootParameters boot_params;
	if(game && game->path)
		boot_params.filename = game->path;

	cpu_thread = std::thread(cpu_thread_entry, boot_params);

	return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info,
							 size_t num_info)
{
	return false;
}

void retro_unload_game(void)
{
	VMManager::Shutdown(false);
	cpu_thread.join();

	InputManager::CloseSources();
	VMManager::Internal::ReleaseMemory();
	VMManager::Internal::ReleaseGlobals();

	if (gs_freeze_data.data)
	{
		free(gs_freeze_data.data);
		gs_freeze_data = {};
	}

	((LayeredSettingsInterface*)Host::GetSettingsInterface())->SetLayer(LayeredSettingsInterface::LAYER_BASE, nullptr);
	new (&GetMTGS()) SysMtgsThread();

//	g_host_display.reset();
}


void retro_run(void)
{
	Options::CheckVariables();

	Input::Update();

	if (Options::upscale_multiplier.Updated())
	{
		retro_system_av_info av_info;
		retro_get_system_av_info(&av_info);
//		s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", Options::upscale_multiplier);
//		VMManager::ApplySettings();
#if 1
		environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
#else
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info.geometry);
		GetMTGS().ClosePlugin();
		GetMTGS().OpenPlugin();
#endif
	}

	//	GetCoreThread().Resume();
	GetMTGS().TryOpenGS();
	while (VMManager::GetState() == VMState::Initializing)
		GetMTGS().StepFrame();

	if (VMManager::GetState() == VMState::Paused)
		VMManager::SetState(VMState::Running);

	RETRO_PERFORMANCE_INIT(pcsx2_run);
	RETRO_PERFORMANCE_START(pcsx2_run);

	if (write_pos > (0x100 << 1))
	{
		std::unique_lock locker(snd_mutex);
		batch_cb(snd_buffer, write_pos >> 1);
		write_pos = 0;
	}

	GetMTGS().StepFrame();

	if (write_pos > (0x100 << 1))
	{
		std::unique_lock locker(snd_mutex);
		batch_cb(snd_buffer, write_pos >> 1);
		write_pos = 0;
	}

	RETRO_PERFORMANCE_STOP(pcsx2_run);
}

size_t retro_serialize_size(void)
{
	freezeData fP = {0, nullptr};

	size_t size = _8mb;
	size += Ps2MemSize::MainRam;
	size += Ps2MemSize::IopRam;
	size += Ps2MemSize::Hardware;
	size += Ps2MemSize::IopHardware;
	size += Ps2MemSize::Scratch;
	size += VU0_MEMSIZE;
	size += VU1_MEMSIZE;
	size += VU0_PROGSIZE;
	size += VU1_PROGSIZE;
	SPU2freeze(FreezeAction::Size, &fP);
	size += fP.size;
	PADfreeze(FreezeAction::Size, &fP);
	size += fP.size;
	GSfreeze(FreezeAction::Size, &fP);
	size += fP.size;

	return size;
}

bool retro_serialize(void* data, size_t size)
{
	cpu_thread_pause();

	VmStateBuffer buffer;
	memSavingState saveme(buffer);
	freezeData fP;

	saveme.FreezeBios();
	saveme.FreezeInternals();

	saveme.FreezeMem(eeMem->Main, sizeof(eeMem->Main));
	saveme.FreezeMem(iopMem->Main, sizeof(iopMem->Main));
	saveme.FreezeMem(eeHw, sizeof(eeHw));
	saveme.FreezeMem(iopHw, sizeof(iopHw));
	saveme.FreezeMem(eeMem->Scratch, sizeof(eeMem->Scratch));
	saveme.FreezeMem(vuRegs[0].Mem, VU0_MEMSIZE);
	saveme.FreezeMem(vuRegs[1].Mem, VU1_MEMSIZE);
	saveme.FreezeMem(vuRegs[0].Micro, VU0_PROGSIZE);
	saveme.FreezeMem(vuRegs[1].Micro, VU1_PROGSIZE);

	fP.size = 0;
	fP.data = nullptr;
	SPU2freeze(FreezeAction::Size, &fP);
	saveme.PrepBlock(fP.size);
	fP.data = saveme.GetBlockPtr();
	SPU2freeze(FreezeAction::Save, &fP);
	saveme.CommitBlock(fP.size);

	fP.size = 0;
	fP.data = nullptr;
	PADfreeze(FreezeAction::Size, &fP);
	saveme.PrepBlock(fP.size);
	fP.data = saveme.GetBlockPtr();
	PADfreeze(FreezeAction::Save, &fP);
	saveme.CommitBlock(fP.size);

	fP.size = 0;
	fP.data = nullptr;
	GSfreeze(FreezeAction::Size, &fP);
	saveme.PrepBlock(fP.size);
	fP.data = saveme.GetBlockPtr();
	GSfreeze(FreezeAction::Save, &fP);
	saveme.CommitBlock(fP.size);

	pxAssert(size >= (size_t)buffer.GetLength());
//	printf("size : %i, buffer: %i\n", size , buffer.GetLength());
	memcpy(data, buffer.GetPtr(), buffer.GetLength());


	VMManager::SetPaused(false);
	return true;
}

bool retro_unserialize(const void* data, size_t size)
{
	cpu_thread_pause();

	VmStateBuffer buffer;
	buffer.MakeRoomFor(size);
	memcpy(buffer.GetPtr(), data, size);
	memLoadingState loadme(buffer);
	freezeData fP;

	loadme.FreezeBios();
	loadme.FreezeInternals();

	SysClearExecutionCache();
	loadme.FreezeMem(eeMem->Main, sizeof(eeMem->Main));
	loadme.FreezeMem(iopMem->Main, sizeof(iopMem->Main));
	loadme.FreezeMem(eeHw, sizeof(eeHw));
	loadme.FreezeMem(iopHw, sizeof(iopHw));
	loadme.FreezeMem(eeMem->Scratch, sizeof(eeMem->Scratch));
	loadme.FreezeMem(vuRegs[0].Mem, VU0_MEMSIZE);
	loadme.FreezeMem(vuRegs[1].Mem, VU1_MEMSIZE);
	loadme.FreezeMem(vuRegs[0].Micro, VU0_PROGSIZE);
	loadme.FreezeMem(vuRegs[1].Micro, VU1_PROGSIZE);

	fP.size = 0;
	fP.data = nullptr;
	SPU2freeze(FreezeAction::Size, &fP);
	loadme.PrepBlock(fP.size);
	fP.data = loadme.GetBlockPtr();
	SPU2freeze(FreezeAction::Load, &fP);
	loadme.CommitBlock(fP.size);

	fP.size = 0;
	fP.data = nullptr;
	PADfreeze(FreezeAction::Size, &fP);
	loadme.PrepBlock(fP.size);
	fP.data = loadme.GetBlockPtr();
	PADfreeze(FreezeAction::Load, &fP);
	loadme.CommitBlock(fP.size);

	fP.size = 0;
	fP.data = nullptr;
	GSfreeze(FreezeAction::Size, &fP);
	loadme.PrepBlock(fP.size);
	fP.data = loadme.GetBlockPtr();
	GSfreeze(FreezeAction::Load, &fP);
	loadme.CommitBlock(fP.size);

	VMManager::SetPaused(false);
	return true;
}

unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

unsigned retro_api_version()
{
	return RETRO_API_VERSION;
}

size_t retro_get_memory_size(unsigned id)
{
	return 0;
}

void* retro_get_memory_data(unsigned id)
{
	return NULL;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
}

void SaveStateBase::InputRecordingFreeze()
{
#if 0
	FreezeTag("InputRecording");
	Freeze(g_FrameCount);
#endif
}

void Host::AddOSDMessage(std::string message, float duration)
{

}
void Host::AddKeyedOSDMessage(std::string key, std::string message, float duration)
{

}
void Host::AddIconOSDMessage(std::string key, const char* icon, const std::string_view& message, float duration)
{

}

std::optional<std::vector<u8>> Host::ReadResourceFile(const char* filename)
{
	const std::string path(Path::Combine(EmuFolders::Resources, filename));
	std::optional<std::vector<u8>> ret(FileSystem::ReadBinaryFile(path.c_str()));
	if (!ret.has_value())
		Console.Error("Failed to read resource file '%s'", filename);
	return ret;
}

std::optional<std::string> Host::ReadResourceFileToString(const char* filename)
{
	const std::string path(Path::Combine(EmuFolders::Resources, filename));
	std::optional<std::string> ret(FileSystem::ReadFileToString(path.c_str()));
	if (!ret.has_value())
		Console.Error("Failed to read resource file to string '%s'", filename);
	return ret;
}

std::optional<std::time_t> Host::GetResourceFileTimestamp(const char* filename)
{
	const std::string path(Path::Combine(EmuFolders::Resources, filename));
	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(filename, &sd))
		return std::nullopt;

	return sd.ModificationTime;
}

void Host::AddFormattedOSDMessage(float duration, const char* format, ...)
{

}
void Host::AddKeyedFormattedOSDMessage(std::string key, float duration, const char* format, ...)
{

}
void Host::RemoveKeyedOSDMessage(std::string key)
{

}
void Host::ClearOSDMessages()
{

}

void Host::ReportErrorAsync(const std::string_view& title, const std::string_view& message)
{
	if (!title.empty() && !message.empty())
	{
		Console.Error(
			"ReportErrorAsync: %.*s: %.*s", static_cast<int>(title.size()), title.data(), static_cast<int>(message.size()), message.data());
	}
	else if (!message.empty())
	{
		Console.Error("ReportErrorAsync: %.*s", static_cast<int>(message.size()), message.data());
	}
}

bool Host::ConfirmMessage(const std::string_view& title, const std::string_view& message)
{
	return true;
}

void Host::ResizeHostDisplay(u32 new_window_width, u32 new_window_height, float new_window_scale)
{
}

void Host::UpdateHostDisplay()
{
}

bool Host::AcquireHostDisplay(RenderAPI api, bool clear_state_on_fail)
{
	return true;
}

HostDisplay::PresentResult Host::BeginPresentFrame(bool frame_skip)
{
	if (write_pos > (0x100 << 1))
	{
		std::unique_lock locker(snd_mutex);
		batch_cb(snd_buffer, write_pos >> 1);
		write_pos = 0;
	}
	return g_host_display->BeginPresent(frame_skip);
}

void Host::EndPresentFrame()
{
	g_host_display->EndPresent();
}

void Host::ReleaseHostDisplay(bool clear_state)
{
	g_host_display.reset();
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
}

void Host::OnGameChanged(const std::string& disc_path, const std::string& elf_override, const std::string& game_serial,
	const std::string& game_name, u32 game_crc)
{
}

void Host::OnSaveStateLoading(const std::string_view& filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view& filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view& filename)
{
}

void Host::CancelGameListRefresh()
{
}

void Host::CPUThreadVSync()
{
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
	CommonHost::LoadSettings(si, lock);
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
	CommonHost::CheckForSettingsChanges(old_config);
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	VMManager::SetState(VMState::Stopping);
}


void FullscreenUI::CheckForConfigChanges(const Pcsx2Config& old_config)
{
}

void CommonHost::Internal::ResetVMHotkeyState()
{
}

void FullscreenUI::OnVMStarted()
{
}

void FullscreenUI::OnVMDestroyed()
{
}

void FullscreenUI::OnRunningGameChanged(std::string path, std::string serial, std::string title, u32 crc)
{
}

bool FullscreenUI::IsInitialized()
{
	return true;
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	WindowInfo wi;
	wi.surface_width = 640;
	wi.surface_height = 480;
	wi.surface_scale = 1.0f;
	wi.type = WindowInfo::Type::Libretro;

	return wi;
}

