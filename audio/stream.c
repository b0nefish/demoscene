#include <devices/ahi.h>
#include <exec/exec.h>
#include <proto/ahi.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "audio/stream.h"
#include "std/debug.h"
#include "std/memory.h"

struct AudioStream {
  /* structures used by AHI */
  struct MsgPort *msgPort;
  struct AHIRequest *ioReq;
  struct AHIAudioCtrl *ctrl;
  struct Hook soundHook;

  /* double buffer stuff */
  int buffer;
  size_t signal;
  struct Task *task;

  /* audio stream file */
  BPTR file;

  /* audio buffers data */
  struct AHISampleInfo sample[2];
  size_t sampleLen;
  size_t sampleFreq;
  size_t sampleWidth;
};

typedef struct DiskSample {
  uint16_t padding : 14;
  uint16_t b16 : 1;
  uint16_t stereo : 1;
  uint16_t rate;
  uint32_t length;
} DiskSampleT;

__saveds ULONG SoundFunc(struct Hook *hook asm("a0"),
                         struct AHIAudioCtrl *actrl asm("a2"),
                         struct AHISoundMessage *smsg asm("a1"))
{
  AudioStreamT *audio = actrl->ahiac_UserData;
  audio->buffer = !audio->buffer;
  AHI_SetSound(0, audio->buffer, 0, 0, actrl, 0L);
  Signal(audio->task, 1L << audio->signal);
  return 0L;
}

void AudioStreamClose(AudioStreamT *audio);

TYPEDECL(AudioStreamT, (FreeFuncT)AudioStreamClose);

/* Try to allocate hardware accordingly to sample properties. */
void AllocHardware(AudioStreamT *audio, DiskSampleT *sample) {
  /* Find best Paula mode. */
  ULONG id = AHI_BestAudioID(AHIDB_MinMixFreq, 22050,
                             AHIDB_AudioID,    0x20000,
                             AHIDB_Bits,       14,
                             AHIDB_HiFi,       0,
                             AHIDB_Panning,    1,
                             AHIDB_Stereo,     sample->stereo,
                             TAG_DONE);

  LOG("Chosen 0x%.8lx AHI AudioID.", id);

  ASSERT(id != AHI_INVALID_ID,
         "Is Paula AHI driver installed?");

  /* WinUAE hack */
  id = 0x1A0000;

  /* Allocate audio hardware. */
  audio->ctrl = AHI_AllocAudio(AHIA_AudioID,   id,
                               AHIA_MixFreq,   22050,
                               AHIA_Channels,  sample->stereo ? 2 : 1,
                               AHIA_Sounds,    2,
                               AHIA_SoundFunc, (ULONG)&audio->soundHook,
                               AHIA_UserData,  (ULONG)audio,
                               TAG_DONE);

  ASSERT(audio->ctrl, "Could not initialize audio hardware.");
}

/* Set-up sample buffers. */
static void AllocSampleBuffers(AudioStreamT *audio, DiskSampleT *sample) {
  int i, sampleType, sampleWidth;

  switch ((sample->b16 << 1) | sample->stereo) {
    case 0:
      sampleType = AHIST_M8S;
      sampleWidth = 1;
      break;

    case 1:
      sampleType = AHIST_S8S;
      sampleWidth = 2;
      break;

    case 2:
      sampleType = AHIST_M16S;
      sampleWidth = 2;
      break;

    case 3:
      sampleType = AHIST_S16S;
      sampleWidth = 4;
      break;

    default:
      PANIC("Unknown sample type.");
  }

  /* Buffer lenght is half a second. */
  audio->sampleLen = sample->rate / 2;
  audio->sampleFreq = sample->rate;
  audio->sampleWidth = sampleWidth;

  for (i = 0; i < 2; i++) {
    audio->sample[i].ahisi_Type = sampleType;
    audio->sample[i].ahisi_Length = audio->sampleLen;
    audio->sample[i].ahisi_Address = MemNew(audio->sampleLen * sampleWidth);
    AHI_LoadSound(i, AHIST_DYNAMICSAMPLE, &audio->sample[i], audio->ctrl);
  }
}

AudioStreamT *AudioStreamOpen(const char *filename) {
  DiskSampleT sample;
  BPTR file;

  /* Open audio stream file and read the header. */
  if (!(file = Open(filename, MODE_OLDFILE))) {
    LOG("File '%s' not found.", filename);
    return NULL;
  }

  ASSERT(Read(file, &sample, sizeof(DiskSampleT)) == 8,
         "File is missing a header.");

  LOG("Audio stream '%s' info: %d bit, %s, %dHz, %ld samples.\n",
      filename,
      sample.b16 ? 16 : 8,
      sample.stereo ? "stereo" : "mono",
      sample.rate,
      sample.length);

  /* Allocate and initailize audio stream structure. */
  {
    AudioStreamT *audio = NewInstance(AudioStreamT);

    audio->file = file;

    /* Allocate a signal for swap-buffer events. */
    audio->signal = AllocSignal(-1);
    audio->buffer = 0;
    audio->task = FindTask(NULL);

    /* Set up a swap buffer callback function. */
    audio->soundHook.h_Entry = &SoundFunc;

    /* Open 'ahi.device' version 4 or greater. */
    audio->msgPort = CreateMsgPort();
    audio->ioReq = (struct AHIRequest *)
      CreateIORequest(audio->msgPort, sizeof(struct AHIRequest));
    audio->ioReq->ahir_Version = 4;

    ASSERT(OpenDevice(AHINAME, AHI_NO_UNIT, (struct IORequest *)audio->ioReq, 0) == 0,
           "Cannot open 'ahi.device' with version not lesser than 4.0.");

    AHIBase = (struct Library *)audio->ioReq->ahir_Std.io_Device;

    AllocHardware(audio, &sample);
    AllocSampleBuffers(audio, &sample);

    return audio;
  }
}

uint32_t AudioStreamGetSignal(AudioStreamT *audio) {
  return audio->signal;
}

size_t AudioStreamFeed(AudioStreamT *audio) {
  size_t requested = audio->sampleLen * audio->sampleWidth;
  size_t obtained = Read(audio->file,
                         audio->sample[audio->buffer].ahisi_Address,
                         requested);

  if (obtained < requested)
    /* Clear rest of buffer */
    memset(audio->sample[audio->buffer].ahisi_Address, 0, requested - obtained);

  return obtained / audio->sampleWidth;
}

bool AudioStreamPlay(AudioStreamT *audio) {
  if (AHI_ControlAudio(audio->ctrl,
                       AHIC_Play, 1,
                       TAG_DONE) == AHIE_OK)
  {
    struct AHIEffMasterVolume masterVol;

    masterVol.ahie_Effect = AHIET_MASTERVOLUME;
    masterVol.ahiemv_Volume = 0x40000;
    AHI_SetEffect(&masterVol, audio->ctrl);

    AudioStreamFeed(audio);

    // The new AHI_PlayA() function is demonstrated...
    AHI_Play(audio->ctrl,
             AHIP_BeginChannel, 0,
             AHIP_Freq,         audio->sampleFreq,
             AHIP_Vol,          0x10000L,
             AHIP_Pan,          0x8000L,
             AHIP_Sound,        0,
             AHIP_Offset,       0,
             AHIP_Length,       0,
             AHIP_EndChannel,   0,
             TAG_DONE);

    return TRUE;
  }

  return FALSE;
}

void AudioStreamStop(AudioStreamT *audio) {
  AHI_ControlAudio(audio->ctrl,
                   AHIC_Play, 0,
                   TAG_DONE);
}

void AudioStreamClose(AudioStreamT *audio) {
  MemUnref(audio->sample[0].ahisi_Address);
  MemUnref(audio->sample[1].ahisi_Address);
  AHI_FreeAudio(audio->ctrl);
  CloseDevice((struct IORequest *)audio->ioReq);
  DeleteIORequest((struct IORequest *)audio->ioReq);
  DeleteMsgPort(audio->msgPort);
  FreeSignal(audio->signal);
  Close(audio->file);
}