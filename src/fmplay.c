#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <iconv.h>

#include <portaudio.h>

#include <common/fmplayer_common.h>
#include <common/fmplayer_file.h>
#include <libopna/opna.h>
#include <libopna/opnatimer.h>

constexpr int SAMPLE_RATE = 55467;

static const char *usage =
    "Usage: fmplay [OPTION...] FILE\n"
    "Play PMD or FMP modules.\n"
    "\n"
    "  -h, --help           show help\n";

static const struct option options[] = {
    { .name = "help", .has_arg = no_argument, .val = 'h' },
    { .name = "no-fade", .has_arg = no_argument, .val = 'F' },
    { .name = "loops", .has_arg = required_argument, .val = 'l' },
    {},
};

struct context {
    struct opna_timer *timer;
    struct fmdriver_work *work;
    uint64_t volume;
    uint8_t loops;
    bool fadeout_enabled;
};

int pa_callback(
    const void *input,
    void *output,
    unsigned long frames,
    const PaStreamCallbackTimeInfo *time_info,
    PaStreamCallbackFlags flags,
    void *userdata
) {
    (void)input;
    (void)time_info;
    (void)flags;
    struct context *ctx = userdata;
    int16_t *out = output;
    memset(out, 0, 2 * sizeof(int16_t) * frames);
    opna_timer_mix(ctx->timer, out, frames);
    if (ctx->fadeout_enabled) {
        for (unsigned long i = 0; i < frames; i++) {
            int volume = ctx->volume >> 16;
            out[2 * i + 0] = (out[2 * i + 0] * volume) >> 16;
            out[2 * i + 1] = (out[2 * i + 1] * volume) >> 16;
            if (ctx->work->loop_cnt >= ctx->loops) {
                ctx->volume = (ctx->volume * 0xffff0000ull) >> 32;
            }
        }
        return ctx->volume > 0 ? paContinue : paComplete;
    } else {
        return ctx->work->loop_cnt < ctx->loops ? paContinue : paComplete;
    }
}

int main(int argc, char **argv) {
    bool fade = true;
    int loops = 1;

    int optchar;
    while ((optchar = getopt_long(argc, argv, "hFl:", options, nullptr)) != -1) {
        switch (optchar) {
        case 'h':
            fprintf(stderr, "%s", usage);
            return 0;
        case 'F':
            fade = false;
            break;
        case 'l':
            loops = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", usage);
            return 1;
        }
    }
    if (optind + 1 != argc) {
        fprintf(stderr, "%s", usage);
        return 1;
    }
    const char *filename = argv[optind];

    enum fmplayer_file_error fmfile_error;
    struct fmplayer_file *fmfile = fmplayer_file_alloc(filename, &fmfile_error);
    if (!fmfile) {
        fprintf(stderr, "cannot load file: %s\n", fmplayer_file_strerror(fmfile_error));
        return 1;
    }

    struct opna opna = {};
    struct opna_timer timer = {};
    struct ppz8 ppz8 = {};
    struct fmdriver_work work = {};
    uint8_t adpcm_ram[OPNA_ADPCM_RAM_SIZE];
    fmplayer_init_work_opna(&work, &ppz8, &opna, &timer, adpcm_ram);
    opna_ssg_set_mix(&opna.ssg, 0x10000);
    opna_ssg_set_ymf288(&opna.ssg, &opna.resampler, false);
    ppz8_set_interpolation(&ppz8, PPZ8_INTERP_SINC);
    opna_fm_set_hires_sin(&opna.fm, false);
    opna_fm_set_hires_env(&opna.fm, false);
    fmplayer_file_load(&work, fmfile, loops);

    static const char *pmd_comment_titles[] = {
        "Title",
        "Composer",
        "Arranger",
    };
    for (int i = 0; ; i++) {
        const char *comment = work.get_comment(&work, i);
        if (!comment) break;
        if (work.comment_mode_pmd && i < 3) {
            printf("%s: ", pmd_comment_titles[i]);
        }

        iconv_t cd = iconv_open("UTF-8", "CP932");
        if (cd != (iconv_t)-1) {
            char *in = (char*)comment;
            size_t in_left = strlen(in);
            char utf8[256];
            char *out = utf8;
            size_t out_left = sizeof(utf8) - 1;
            for (;;) {
                if (iconv(cd, &in, &in_left, &out, &out_left) == (size_t)-1) {
                    goto iconv_done;
                }
                if (in_left == 0) break;
            }
            iconv(cd, nullptr, nullptr, &out, &out_left);
        iconv_done:
            iconv_close(cd);
            *out = 0;
            printf("%s", utf8);
        }
        printf("\n");
    }

    struct context ctx = {
        .timer = &timer,
        .work = &work,
        .volume = 0x1'00000000,
        .loops = loops,
        .fadeout_enabled = fade,
    };

    PaError pa_error;
    if ((pa_error = Pa_Initialize()) != paNoError) {
        fprintf(stderr, "cannot initialize audio: %s\n", Pa_GetErrorText(pa_error));
        fmplayer_file_free(fmfile);
        return 1;
    }
    PaStream *pa_stream;
    pa_error = Pa_OpenDefaultStream(
        &pa_stream,
        0,
        2,
        paInt16,
        SAMPLE_RATE,
        paFramesPerBufferUnspecified,
        pa_callback,
        &ctx);
    if (pa_error != paNoError) {
        fprintf(stderr, "cannot open audio stream: %s\n", Pa_GetErrorText(pa_error));
        Pa_Terminate();
        fmplayer_file_free(fmfile);
        return 1;
    }
    pa_error = Pa_StartStream(pa_stream);
    if (pa_error != paNoError) {
        fprintf(stderr, "cannot start audio stream: %s\n", Pa_GetErrorText(pa_error));
        Pa_CloseStream(pa_stream);
        Pa_Terminate();
        fmplayer_file_free(fmfile);
        return 1;
    }

    while (Pa_IsStreamActive(pa_stream) == 1) {
        printf("\rTimerB: %d %d", work.timerb, work.timerb_cnt);
        fflush(stdout);
        Pa_Sleep(1'000);
    }
    printf("\n");

    Pa_CloseStream(pa_stream);
    Pa_Terminate();
    fmplayer_file_free(fmfile);
    return 0;
}
