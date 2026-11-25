#include "audio/voice_mixer.h"

#include "base/prof.h"

#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <cmath>

#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#endif

namespace gt3r {

namespace {

constexpr s32 kEnvMax = 0x7FFF;
// Volume changes are slewed so register-style updates at frame rate do not
// produce zipper noise: full scale in ~5 ms. (The SPU2 applies register
// writes at once; the game limits its own steps for engine notes.)
constexpr s32 kVolSlew = 0x3FFF / 256 + 1;
constexpr u8 kFlagEnd = 1, kFlagRepeat = 2, kFlagLoopStart = 4;

// The SPU's 4-point Gaussian interpolation table (hardware constant, as
// documented in "psx-spx"; the SPU2 uses the same filter). Indexed by bits
// 4..11 of the pitch counter.
constexpr s16 kGauss[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7,
    8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24,
    25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56,
    58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111,
    116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199,
    205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328,
    338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512,
    525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761,
    779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089,
    1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508,
    1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031,
    2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668,
    2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427,
    3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315,
    4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332,
    5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475,
    6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7652, 7734,
    7816, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097,
    9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542,
    10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045,
    12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577,
    13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104,
    15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592,
    16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001,
    18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295,
    19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439,
    20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399,
    21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148,
    22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662,
    22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927,
    22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

} // namespace

VoiceMixer::VoiceMixer() {
    for (auto& v : voices_) v.gen = 1;
    changes_.reserve(64);
#if defined(__vita__)
    helper_go_ = SDL_CreateSemaphore(0);
    helper_done_ = SDL_CreateSemaphore(0);
    if (helper_go_ && helper_done_) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&helper_main));
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "voice mixer helper");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 128 * 1024);
        helper_ = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
    }
#endif
}

VoiceMixer::~VoiceMixer() {
#if defined(__vita__)
    if (helper_) {
        helper_quit_.store(true);
        SDL_SignalSemaphore(helper_go_);
        SDL_WaitThread(helper_, nullptr);
    }
    if (helper_go_) SDL_DestroySemaphore(helper_go_);
    if (helper_done_) SDL_DestroySemaphore(helper_done_);
#endif
}

VoiceMixer::Voice* VoiceMixer::find(VoiceId id) {
    u32 idx = id.value & 0xFF;
    if (!id.valid() || idx >= u32(kVoices)) return nullptr;
    Voice& v = voices_[idx];
    return (v.gen == (id.value >> 8) && v.phase != Phase::Off) ? &v : nullptr;
}

const VoiceMixer::Voice* VoiceMixer::find(VoiceId id) const { return const_cast<VoiceMixer*>(this)->find(id); }

VoiceId VoiceMixer::key_on(const VoiceParams& p) {
    if (!p.data || p.start + PsAdpcmDecoder::kFrameBytes > p.data->size()) return {};
    std::lock_guard lock(mutex_);
    // A free voice first. Otherwise steal like the game's allocator: only
    // voices whose priority value is >= the request's, preferring the
    // highest value, then the oldest.
    int pick = -1;
    for (int i = 0; i < kVoices && pick < 0; ++i)
        if (voices_[i].phase == Phase::Off) pick = i;
    if (pick < 0) {
        int best_prio = -1;
        u64 best_age = ~0ull;
        for (int i = 0; i < kVoices; ++i) {
            const Voice& v = voices_[i];
            if (v.priority < p.priority) continue;
            if (v.priority > best_prio || (v.priority == best_prio && v.age < best_age))
                best_prio = v.priority, best_age = v.age, pick = i;
        }
    }
    if (pick < 0) return {};
    Voice& v = voices_[pick];
    u32 gen = (v.gen + 1) & 0xFFFFFF;
    v = Voice{};
    v.gen = gen ? gen : 1;
    v.phase = Phase::Attack;
    v.data = p.data;
    v.addr = v.loop_addr = p.start;
    v.hw_pitch = hardware_pitch(p.pitch);
    v.vol_l = v.target_l = p.vol_l;
    v.vol_r = v.target_r = p.vol_r;
    v.adsr1 = p.adsr1;
    v.adsr2 = p.adsr2;
    v.reverb = p.reverb;
    v.priority = p.priority;
    v.bus = u8(p.bus);
    v.age = ++clock_;
    const VoiceId id{u32(pick) | (v.gen << 8)};
    if (rendering_) {
        Change c(Change::Kind::KeyOn, id);
        c.voice = v;
        changes_.push_back(std::move(c));
    }
    return id;
}

u16 VoiceMixer::hardware_pitch(u16 game_pitch) { return u16(std::min<u32>((u32(game_pitch) * 0xEB3) >> 12, 0x4000)); }

void VoiceMixer::key_off(VoiceId id) {
    std::lock_guard lock(mutex_);
    change({Change::Kind::KeyOff, id});
}

void VoiceMixer::stop(VoiceId id) {
    std::lock_guard lock(mutex_);
    change({Change::Kind::Stop, id});
}

void VoiceMixer::stop_all() {
    std::lock_guard lock(mutex_);
    change({Change::Kind::StopAll});
}

void VoiceMixer::set_pitch(VoiceId id, u16 pitch) {
    std::lock_guard lock(mutex_);
    change({Change::Kind::Pitch, id, pitch});
}

void VoiceMixer::set_volume(VoiceId id, s16 l, s16 r) {
    std::lock_guard lock(mutex_);
    change({Change::Kind::Volume, id, 0, l, r});
}

void VoiceMixer::set_priority(VoiceId id, u8 priority) {
    std::lock_guard lock(mutex_);
    change({Change::Kind::Priority, id, priority});
}

// The table takes the change at once (the game's next calls see it). While a block renders
// from its own copy, the change is also kept and made again on the rendered voices after it.
// The reverb's are kept only: the block's reverb_ is the mixing thread's until it ends.
void VoiceMixer::change(Change c) {
    const bool reverb = c.kind == Change::Kind::Reverb || c.kind == Change::Kind::ReverbDepth;
    if (!rendering_ || !reverb) apply(c);
    if (rendering_) changes_.push_back(std::move(c));
}

void VoiceMixer::apply(const Change& c) {
    using Kind = Change::Kind;
    switch (c.kind) {
    case Kind::KeyOn: voices_[c.id.value & 0xFF] = c.voice; return;
    case Kind::StopAll:
        for (auto& v : voices_) {
            v.phase = Phase::Off;
            v.data.reset();
        }
        return;
    case Kind::Reverb:
        if (c.value != reverb_.mode()) reverb_.set_mode(c.value);
        reverb_.set_depth(c.l, c.r);
        return;
    case Kind::ReverbDepth: reverb_.set_depth(c.l, c.r); return;
    default: break;
    }
    Voice* v = find(c.id);
    if (!v) return;
    switch (c.kind) {
    case Kind::KeyOff:
        if (v->phase != Phase::Release) v->env_counter = 0;
        v->phase = Phase::Release;
        break;
    case Kind::Stop:
        v->phase = Phase::Off;
        v->data.reset();
        break;
    case Kind::Pitch: v->hw_pitch = hardware_pitch(u16(c.value)); break;
    case Kind::Volume:
        v->target_l = c.l;
        v->target_r = c.r;
        break;
    case Kind::Priority: v->priority = u8(c.value); break;
    default: break;
    }
}

bool VoiceMixer::playing(VoiceId id) const {
    std::lock_guard lock(mutex_);
    return find(id) != nullptr;
}

bool VoiceMixer::releasing(VoiceId id) const {
    std::lock_guard lock(mutex_);
    const Voice* v = find(id);
    return v && v->phase == Phase::Release;
}

int VoiceMixer::active_voices() const {
    std::lock_guard lock(mutex_);
    return int(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.phase != Phase::Off; }));
}

float VoiceMixer::envelope(VoiceId id) const {
    std::lock_guard lock(mutex_);
    const Voice* v = find(id);
    return v ? float(v->env) / kEnvMax : 0.0f;
}

void VoiceMixer::set_master_volume(float v) {
    std::lock_guard lock(mutex_);
    master_ = std::clamp(v, 0.0f, 2.0f);
}

void VoiceMixer::set_bus_gain(VoiceBus bus, float gain) {
    std::lock_guard lock(mutex_);
    bus_gain_[size_t(bus)] = s32(std::lround(std::clamp(gain, 0.0f, 1.0f) * 65536.0f));
}

float VoiceMixer::bus_gain(VoiceBus bus) const {
    std::lock_guard lock(mutex_);
    return float(bus_gain_[size_t(bus)]) / 65536.0f;
}

void VoiceMixer::set_interpolation(Interpolation i) {
    std::lock_guard lock(mutex_);
    interp_ = i;
}

void VoiceMixer::set_reverb(int mode, s16 depth_l, s16 depth_r) {
    std::lock_guard lock(mutex_);
    reverb_mode_ = mode, reverb_depth_ = depth_l;
    change({Change::Kind::Reverb, {}, mode, depth_l, depth_r});
}

void VoiceMixer::set_reverb_depth(s16 depth_l, s16 depth_r) {
    std::lock_guard lock(mutex_);
    reverb_depth_ = depth_l;
    change({Change::Kind::ReverbDepth, {}, 0, depth_l, depth_r});
}

int VoiceMixer::reverb_mode() const {
    std::lock_guard lock(mutex_);
    return reverb_mode_;
}

s16 VoiceMixer::reverb_depth() const {
    std::lock_guard lock(mutex_);
    return reverb_depth_;
}

void VoiceMixer::set_tick_hook(std::function<void()> hook, u32 samples_per_tick) {
    std::lock_guard lock(hook_mutex_);
    hook_ = std::move(hook);
    samples_per_tick_ = hook_ ? std::max<u32>(1, samples_per_tick) : 0;
    until_tick_ = 0;
}

// Pulls the next decoded sample, following the frame flags the way the SPU
// does: a loop-start frame latches the loop address; after an end frame the
// voice jumps to the loop address if the repeat flag is set, else it stops.
s32 VoiceMixer::next_sample(Voice& v) {
    if (v.block_pos >= 28) {
        if (v.frame_flags & kFlagEnd) {
            if (v.frame_flags & kFlagRepeat) {
                v.addr = v.loop_addr;
            } else {
                v.ended = true;
                return 0;
            }
        }
        if (v.addr + PsAdpcmDecoder::kFrameBytes > v.data->size()) {
            v.ended = true;
            return 0;
        }
        const u8* f = v.data->data() + v.addr;
        if (f[1] & kFlagLoopStart) v.loop_addr = v.addr;
        v.frame_flags = v.dec.decode_frame(f, v.block);
        v.addr += PsAdpcmDecoder::kFrameBytes;
        v.block_pos = 0;
    }
    return v.block[v.block_pos++];
}

// One envelope step in the hardware's counter model. A rate (0..127) picks a
// step and a counter increment: rates below 44 move every sample by a step
// scaled up by 2^(11 - rate/4); higher rates move by the base step every
// 2^(rate/4 - 11) samples. A rate with every bit of its field set never
// moves. Exponential decrease scales the step by the level; exponential
// increase slows down above 0x6000. Returns false once the level has hit
// the end of its range in the direction of travel.
bool VoiceMixer::env_tick(Voice& v, int rate, int rate_mask, bool exponential, bool decrease) {
    s32 step = decrease ? -8 + (rate & 3) : 7 - (rate & 3);
    u32 inc = 0x8000;
    if (rate < 44) {
        step <<= 11 - (rate >> 2);
    } else if (rate >= 48) {
        int shift = (rate >> 2) - 11;
        inc = shift >= 16 ? 0 : 0x8000u >> shift;
        if ((rate & rate_mask) != rate_mask) inc = std::max<u32>(inc, 1);
    }
    if (exponential) {
        if (decrease) {
            step = s32((s64(step) * v.env) >> 15);
        } else if (v.env > 0x6000) {
            if (rate < 40) step >>= 2;
            else if (rate >= 44) inc >>= 2;
            else step >>= 1, inc >>= 1;
        }
    }
    v.env_counter += inc;
    if (v.env_counter & 0x8000) {
        v.env_counter = 0;
        v.env = std::clamp(v.env + step, 0, kEnvMax);
    }
    return decrease ? v.env > 0 : v.env < kEnvMax;
}

void VoiceMixer::step_envelope(Voice& v) {
    switch (v.phase) {
    case Phase::Attack:
        if (!env_tick(v, (v.adsr1 >> 8) & 0x7F, 0x7F, (v.adsr1 & 0x8000) != 0, false)) {
            v.phase = Phase::Decay;
            v.env_counter = 0;
        }
        break;
    case Phase::Decay: {
        // The level is compared before stepping, so sustain level 15
        // (0x8000, above the maximum) ends the decay at once at full level.
        s32 sustain = std::min(((v.adsr1 & 0xF) + 1) << 11, kEnvMax);
        if (v.env <= sustain) {
            v.phase = Phase::Sustain;
            v.env_counter = 0;
            break;
        }
        env_tick(v, ((v.adsr1 >> 4) & 0xF) << 2, 0xF << 2, true, true);
        break;
    }
    case Phase::Sustain:
        env_tick(v, (v.adsr2 >> 6) & 0x7F, 0x7F, (v.adsr2 & 0x8000) != 0, (v.adsr2 & 0x4000) != 0);
        break;
    case Phase::Release:
        if (!env_tick(v, (v.adsr2 & 0x1F) << 2, 0x1F << 2, (v.adsr2 & 0x20) != 0, true)) v.phase = Phase::Off;
        break;
    case Phase::Off: break;
    }
}

void VoiceMixer::mix(s16* out, size_t frames) {
    std::lock_guard mixing(mix_mutex_);
    size_t done = 0;
    while (done < frames) {
        std::function<void()> hook;
        size_t n = frames - done;
        {
            std::lock_guard lock(hook_mutex_);
            if (hook_) {
                if (until_tick_ == 0) {
                    hook = hook_;
                    until_tick_ = samples_per_tick_;
                }
                n = std::min<size_t>(n, until_tick_);
                until_tick_ -= u32(n);
            }
        }
        if (hook) hook();
        render_block(out + 2 * done, n);
        done += n;
    }
}

// One block: the voices as they are now, rendered without the lock; then the rendered voices
// and, after them, the changes made meanwhile (in their order).
void VoiceMixer::render_block(s16* out, size_t frames) {
    BlockParams p;
    {
        std::lock_guard lock(mutex_);
        block_ = voices_;
        p.master = s32(master_ * 4096.0f);
        p.gauss = interp_ == Interpolation::Gaussian;
        p.bus_gain = bus_gain_;
        rendering_ = true;
    }
    render(out, frames, p);
    std::lock_guard lock(mutex_);
    voices_.swap(block_);
    rendering_ = false;
    for (const Change& c : changes_) apply(c);
    changes_.clear();
}

#if defined(__vita__)
// The PlayStation Vita mixes the race's ~46 voices in the SPU2's own way on a 444 MHz core: that
// took 96% of it and the sound broke up. Its voices are worked out once every two output samples
// (24 kHz), the first sample of each pair halfway from the pair before; the reverb still runs on
// every sample (its own timing and filters).
constexpr bool kHalfRate = true;
#else
constexpr bool kHalfRate = false;
#endif

void VoiceMixer::render_voices(size_t first, size_t last, size_t frames, bool gauss, std::vector<PassSums>& sums) {
    // Voice by voice, each through the block's passes: the same work as the passes over every voice in turn (each
    // voice's state is its own), with the voice's state kept close, and any share of the voices on any thread.
    for (size_t vi = first; vi < last && vi < block_.size(); ++vi) {
        Voice& v = block_[vi];
        if (v.phase == Phase::Off) continue;
        size_t n = 0;
        for (size_t pi = 0; pi < sums.size(); ++pi) {
            // Output samples this pass of the voices: 1, or 2 at half rate (the block's odd last one alone).
            const int step = kHalfRate && n + 1 < frames ? 2 : 1;
            n += size_t(step);
            // Advance the source position by pitch/0x1000 samples (a sample's worth each output sample).
            v.counter += u32(v.hw_pitch) * u32(step);
            while (v.counter >= 0x1000 && !v.ended) {
                v.counter -= 0x1000;
                s32 s = next_sample(v);
                v.hist[0] = v.hist[1];
                v.hist[1] = v.hist[2];
                v.hist[2] = v.hist[3];
                v.hist[3] = s;
            }
            if (v.ended) {
                v.phase = Phase::Off;
                v.data.reset();
                break; // (nothing this pass or after)
            }
            const s32 p0 = v.hist[0], p1 = v.hist[1], p2 = v.hist[2], p3 = v.hist[3];
            s32 sample;
            if (gauss) {
                // Four taps from the table, each product scaled down on its
                // own as the hardware does.
                const u32 i = (v.counter >> 4) & 0xFF;
                sample = ((kGauss[0x0FF - i] * p0) >> 15) + ((kGauss[0x1FF - i] * p1) >> 15) +
                         ((kGauss[0x100 + i] * p2) >> 15) + ((kGauss[i] * p3) >> 15);
            } else {
                // 4-point Catmull-Rom between hist[1] and hist[2].
                s64 a = -p0 + 3 * p1 - 3 * p2 + p3;
                s64 b = 2 * p0 - 5 * p1 + 4 * p2 - p3;
                s64 c = -p0 + p2;
                s64 t = s32(v.counter);
                s64 val = (((a * t >> 12) * t >> 12) * t >> 12) + ((b * t >> 12) * t >> 12) + (c * t >> 12) + 2 * p1;
                sample = s32(std::clamp<s64>(val / 2, -32768, 32767));
            }

            for (int k = 0; k < step; ++k) step_envelope(v); // (the envelope's steps are per output sample)
            s32 e = s32((s64(sample) * v.env) >> 15);

            // Slew volume towards the target.
            const s32 slew = kVolSlew * step;
            v.vol_l += std::clamp(v.target_l - v.vol_l, -slew, slew);
            v.vol_r += std::clamp(v.target_r - v.vol_r, -slew, slew);
            s32 l = (e * v.vol_l) >> 14, r = (e * v.vol_r) >> 14;
            PassSums& s = sums[pi];
            s.l[v.bus] += l;
            s.r[v.bus] += r;
            if (v.reverb) s.wl[v.bus] += l, s.wr[v.bus] += r;
            if (v.phase == Phase::Off) {
                v.data.reset();
                break; // (its envelope ended this pass: nothing after)
            }
        }
    }
}

void VoiceMixer::render(s16* out, size_t frames, const BlockParams& p) {
    const s32 master = p.master;
    const bool gauss = p.gauss;
    const bool rev = reverb_.mode() != SpuReverb::Off;
    // Each bus sums its voices on its own and is scaled by its gain before the mix (a
    // gain of 1 leaves the sum unchanged), so the reverb return follows the gains too.
    auto scaled = [&p](const s32 (&sum)[2]) {
        return s32((s64(sum[0]) * p.bus_gain[0] + s64(sum[1]) * p.bus_gain[1]) >> 16);
    };
    // The block's passes: an output sample each, or a pair at half rate (an odd last sample alone).
    const size_t passes = kHalfRate ? frames / 2 + (frames & 1) : frames;
    sums_.assign(passes, PassSums{}); // (its room kept: no allocation once grown)
#if defined(__vita__)
    if (helper_) {
        // The voices after the middle of the playing ones go to the helper; the sums add up the same either way.
        size_t playing = 0;
        for (const Voice& v : block_) playing += v.phase != Phase::Off;
        size_t mid = 0;
        for (size_t seen = 0; mid < block_.size() && seen < (playing + 1) / 2; ++mid) seen += block_[mid].phase != Phase::Off;
        helper_sums_.assign(passes, PassSums{});
        job_first_ = mid, job_last_ = block_.size(), job_frames_ = frames, job_gauss_ = gauss;
        SDL_SignalSemaphore(helper_go_); // (a system call: the job's fields are out before it)
        render_voices(0, mid, frames, gauss, sums_);
        SDL_WaitSemaphore(helper_done_);
        for (size_t i = 0; i < passes; ++i) {
            PassSums& a = sums_[i];
            const PassSums& b = helper_sums_[i];
            for (int k = 0; k < 2; ++k) a.l[k] += b.l[k], a.r[k] += b.r[k], a.wl[k] += b.wl[k], a.wr[k] += b.wr[k];
        }
    } else
#endif
        render_voices(0, block_.size(), frames, gauss, sums_);
    size_t n = 0;
    for (size_t pi = 0; pi < passes; ++pi) {
        const int step = kHalfRate && n + 1 < frames ? 2 : 1;
        const PassSums& s = sums_[pi];
        const s32 dry[2] = {scaled(s.l), scaled(s.r)};
        const s32 wet[2] = {rev ? scaled(s.wl) : 0, rev ? scaled(s.wr) : 0};
        for (int k = 0; k < step; ++k) {
            const bool half = k == 0 && step == 2; // a pair's first sample: halfway from the pair before
            s32 acc_l = half ? (last_dry_[0] + dry[0]) / 2 : dry[0];
            s32 acc_r = half ? (last_dry_[1] + dry[1]) / 2 : dry[1];
            if (rev) {
                s32 rl, rr;
                reverb_.process(half ? (last_wet_[0] + wet[0]) / 2 : wet[0], half ? (last_wet_[1] + wet[1]) / 2 : wet[1],
                                rl, rr);
                acc_l += rl;
                acc_r += rr;
            }
            const size_t f = n + size_t(k);
            out[2 * f] = s16(std::clamp((acc_l * master) >> 12, -32768, 32767));
            out[2 * f + 1] = s16(std::clamp((acc_r * master) >> 12, -32768, 32767));
        }
        last_dry_[0] = dry[0], last_dry_[1] = dry[1], last_wet_[0] = wet[0], last_wet_[1] = wet[1];
        n += size_t(step);
    }
}

#if defined(__vita__)
int SDLCALL VoiceMixer::helper_main(void* self) {
    static_cast<VoiceMixer*>(self)->helper_loop();
    return 0;
}

void VoiceMixer::helper_loop() {
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2); // (the third core)
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);                       // (the audio thread's own)
    prof::set_thread_off();
    for (;;) {
        SDL_WaitSemaphore(helper_go_);
        if (helper_quit_.load()) return;
        render_voices(job_first_, job_last_, job_frames_, job_gauss_, helper_sums_);
        SDL_SignalSemaphore(helper_done_); // (its voices and sums out before it)
    }
}
#endif

} // namespace gt3r
