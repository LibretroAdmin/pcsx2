
#include "SPU2/Global.h"
#include "SPU2/spu2.h"
#include "SPU2/SndOut.h"

void SndBuffer::Cleanup()
{
}

void SndBuffer::ClearContents()
{
}

void SndBuffer::ResetBuffers()
{
}

void SPU2::SetOutputPaused(bool paused)
{
}

void SPU2::SetOutputVolume(s32 volume)
{
}

bool SPU2::IsAudioCaptureActive()
{
	return false;
}

void SPU2::SetAudioCaptureActive(bool active)
{
}

u32 OutputModule = 0;
int FindOutputModuleById(const char* omodid)
{
	return 0;
}

void DspUpdate()
{
}

s32 DspLoadLibrary(wchar_t* fileName, int modnum)
{
	return 0;
}
