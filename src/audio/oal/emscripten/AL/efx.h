/*
 * AL/efx.h — compatibility shim for the emscripten build.
 *
 * Emscripten implements OpenAL in JavaScript over WebAudio and does not provide the EFX
 * effects extension, nor its header. reVC only ever reaches EFX through alGetProcAddress,
 * behind an alcIsExtensionPresent(ALC_EXT_EFX_NAME) check that fails in the browser, so the
 * function pointers below stay null and are never called. The declarations exist purely so
 * the four audio translation units compile unchanged.
 *
 * Consequence: no environmental reverb in the browser build. Everything else — music,
 * radio, speech, positional SFX — goes through plain OpenAL and works.
 *
 * Token values follow the OpenAL EFX specification, so nothing here has to change if
 * emscripten ever grows a real EFX implementation.
 */
#ifndef AL_EFX_H
#define AL_EFX_H

#include <AL/al.h>
#include <AL/alc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emscripten's al.h omits it — there is only one calling convention on wasm anyway. */
#ifndef AL_APIENTRY
#define AL_APIENTRY
#endif

#define ALC_EXT_EFX_NAME                         "ALC_EXT_EFX"

#define ALC_EFX_MAJOR_VERSION                    0x20001
#define ALC_EFX_MINOR_VERSION                    0x20002
#define ALC_MAX_AUXILIARY_SENDS                  0x20003

/* Source properties */
#define AL_DIRECT_FILTER                         0x20005
#define AL_AUXILIARY_SEND_FILTER                 0x20006
#define AL_AIR_ABSORPTION_FACTOR                 0x20007
#define AL_ROOM_ROLLOFF_FACTOR                   0x20008
#define AL_CONE_OUTER_GAINHF                     0x20009

/* Effect slot */
#define AL_EFFECTSLOT_EFFECT                     0x0001
#define AL_EFFECTSLOT_GAIN                       0x0002
#define AL_EFFECTSLOT_AUXILIARY_SEND_AUTO        0x0003
#define AL_EFFECTSLOT_NULL                       0x0000

/* Effect type */
#define AL_EFFECT_FIRST_PARAMETER                0x0000
#define AL_EFFECT_LAST_PARAMETER                 0x8000
#define AL_EFFECT_TYPE                           0x8001
#define AL_EFFECT_NULL                           0x0000
#define AL_EFFECT_REVERB                         0x0001
#define AL_EFFECT_EAXREVERB                      0x8000

/* Filter type */
#define AL_FILTER_FIRST_PARAMETER                0x0000
#define AL_FILTER_LAST_PARAMETER                 0x8000
#define AL_FILTER_TYPE                           0x8001
#define AL_FILTER_NULL                           0x0000
#define AL_FILTER_LOWPASS                        0x0001

#define AL_LOWPASS_GAIN                          0x0001
#define AL_LOWPASS_GAINHF                        0x0002

/* Standard reverb */
#define AL_REVERB_DENSITY                        0x0001
#define AL_REVERB_DIFFUSION                      0x0002
#define AL_REVERB_GAIN                           0x0003
#define AL_REVERB_GAINHF                         0x0004
#define AL_REVERB_DECAY_TIME                     0x0005
#define AL_REVERB_DECAY_HFRATIO                  0x0006
#define AL_REVERB_REFLECTIONS_GAIN               0x0007
#define AL_REVERB_REFLECTIONS_DELAY              0x0008
#define AL_REVERB_LATE_REVERB_GAIN               0x0009
#define AL_REVERB_LATE_REVERB_DELAY              0x000A
#define AL_REVERB_AIR_ABSORPTION_GAINHF          0x000B
#define AL_REVERB_ROOM_ROLLOFF_FACTOR            0x000C
#define AL_REVERB_DECAY_HFLIMIT                  0x000D

/* EAX reverb */
#define AL_EAXREVERB_DENSITY                     0x0001
#define AL_EAXREVERB_DIFFUSION                   0x0002
#define AL_EAXREVERB_GAIN                        0x0003
#define AL_EAXREVERB_GAINHF                      0x0004
#define AL_EAXREVERB_GAINLF                      0x0005
#define AL_EAXREVERB_DECAY_TIME                  0x0006
#define AL_EAXREVERB_DECAY_HFRATIO               0x0007
#define AL_EAXREVERB_DECAY_LFRATIO               0x0008
#define AL_EAXREVERB_REFLECTIONS_GAIN            0x0009
#define AL_EAXREVERB_REFLECTIONS_DELAY           0x000A
#define AL_EAXREVERB_REFLECTIONS_PAN             0x000B
#define AL_EAXREVERB_LATE_REVERB_GAIN            0x000C
#define AL_EAXREVERB_LATE_REVERB_DELAY           0x000D
#define AL_EAXREVERB_LATE_REVERB_PAN             0x000E
#define AL_EAXREVERB_ECHO_TIME                   0x000F
#define AL_EAXREVERB_ECHO_DEPTH                  0x0010
#define AL_EAXREVERB_MODULATION_TIME             0x0011
#define AL_EAXREVERB_MODULATION_DEPTH            0x0012
#define AL_EAXREVERB_AIR_ABSORPTION_GAINHF       0x0013
#define AL_EAXREVERB_HFREFERENCE                 0x0014
#define AL_EAXREVERB_LFREFERENCE                 0x0015
#define AL_EAXREVERB_ROOM_ROLLOFF_FACTOR         0x0016
#define AL_EAXREVERB_DECAY_HFLIMIT               0x0017

#define AL_EAXREVERB_MIN_REFLECTIONS_GAIN        0.0f
#define AL_EAXREVERB_MAX_REFLECTIONS_GAIN        3.16f
#define AL_EAXREVERB_MIN_LATE_REVERB_GAIN        0.0f
#define AL_EAXREVERB_MAX_LATE_REVERB_GAIN        10.0f
#define AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF   0.892f
#define AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF   1.0f

typedef void (AL_APIENTRY *LPALGENEFFECTS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY *LPALDELETEEFFECTS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY *LPALISEFFECT)(ALuint);
typedef void (AL_APIENTRY *LPALEFFECTI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY *LPALEFFECTIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY *LPALEFFECTF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY *LPALEFFECTFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY *LPALGETEFFECTI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETEFFECTIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETEFFECTF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY *LPALGETEFFECTFV)(ALuint, ALenum, ALfloat*);

typedef void (AL_APIENTRY *LPALGENFILTERS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY *LPALDELETEFILTERS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY *LPALISFILTER)(ALuint);
typedef void (AL_APIENTRY *LPALFILTERI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY *LPALFILTERIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY *LPALFILTERF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY *LPALFILTERFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY *LPALGETFILTERI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETFILTERIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETFILTERF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY *LPALGETFILTERFV)(ALuint, ALenum, ALfloat*);

typedef void (AL_APIENTRY *LPALGENAUXILIARYEFFECTSLOTS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY *LPALDELETEAUXILIARYEFFECTSLOTS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY *LPALISAUXILIARYEFFECTSLOT)(ALuint);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTFV)(ALuint, ALenum, ALfloat*);

#ifdef __cplusplus
}
#endif

#endif /* AL_EFX_H */
